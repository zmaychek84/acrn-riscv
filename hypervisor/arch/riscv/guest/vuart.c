/*-
 * Copyright (c) 2012 NetApp, Inc.
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
 * Copyright (c) 2024 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <types.h>
#include <uart16550.h>
#include <console.h>
#include <asm/guest/vuart.h>
#include <asm/guest/vm.h>
#include <logmsg.h>

#define init_vuart_lock(vu)	spinlock_init(&((vu)->lock))
#define obtain_vuart_lock(vu, flags)	spinlock_irqsave_obtain(&((vu)->lock), &(flags))
#define release_vuart_lock(vu, flags)	spinlock_irqrestore_release(&((vu)->lock), (flags))

static inline void reset_fifo(struct vuart_fifo *fifo)
{
	fifo->rindex = 0U;
	fifo->windex = 0U;
	fifo->num = 0U;
}

static inline void fifo_putchar(struct vuart_fifo *fifo, char ch)
{
	fifo->buf[fifo->windex] = ch;
	if (fifo->num < fifo->size) {
		fifo->windex = (fifo->windex + 1U) % fifo->size;
		fifo->num++;
	} else {
		fifo->rindex = (fifo->rindex + 1U) % fifo->size;
		fifo->windex = (fifo->windex + 1U) % fifo->size;
	}
}

static inline char fifo_getchar(struct vuart_fifo *fifo)
{
	char c = -1;

	if (fifo->num > 0U) {
		c = fifo->buf[fifo->rindex];
		fifo->rindex = (fifo->rindex + 1U) % fifo->size;
		fifo->num--;
	}
	return c;
}

static inline uint32_t fifo_numchars(const struct vuart_fifo *fifo)
{
	return fifo->num;
}

static inline bool fifo_isfull(const struct vuart_fifo *fifo)
{
	bool ret = false;
	/* When the FIFO has less than 16 empty bytes, it should be
	 * mask as full. As when the 16550 driver in OS receive the
	 * THRE interrupt, it will directly send 16 bytes without
	 * checking the LSR(THRE) */

	/* Desired value should be 16 bytes, but to improve
	 * fault-tolerant, enlarge 16 to 64. So that even the THRE
	 * interrupt is raised by mistake, only if it less than 4
	 * times, data in FIFO will not be overwritten. */
	if ((fifo->size - fifo->num) < 64U) {
		ret = true;
	}
	return ret;
}

void vuart_putchar(struct acrn_vuart *vu, char ch)
{
	uint64_t rflags;

	obtain_vuart_lock(vu, rflags);
	fifo_putchar(&vu->rxfifo, ch);
	vu->lsr |= LSR_DR;
	release_vuart_lock(vu, rflags);
}

char vuart_getchar(struct acrn_vuart *vu)
{
	uint64_t rflags;
	char c;

	obtain_vuart_lock(vu, rflags);
	c = fifo_getchar(&vu->txfifo);
	release_vuart_lock(vu, rflags);
	return c;
}

static inline void init_fifo(struct acrn_vuart *vu)
{
	vu->txfifo.buf = vu->vuart_tx_buf;
	vu->rxfifo.buf = vu->vuart_rx_buf;
	vu->txfifo.size = TX_BUF_SIZE;
	vu->rxfifo.size = RX_BUF_SIZE;
	reset_fifo(&(vu->txfifo));
	reset_fifo(&(vu->rxfifo));
}

/*
 * The IIR returns a prioritized interrupt reason:
 * - receive data available
 * - transmit holding register empty
 *
 * Return an interrupt reason if one is available.
 */
static uint8_t vuart_intr_reason(struct acrn_vuart *vu)
{
	uint8_t ret;

	if (((vu->lsr & LSR_INT_ANY) != 0U) && ((vu->ier & IER_ELSI) != 0U)) {
		ret = IIR_RLS;
	} else if (((vu->lsr & LSR_DR) != 0U) && ((vu->ier & IER_ERBFI) != 0U)) {
		ret = IIR_RXRDY;
	} else if (vu->thre_int_pending && ((vu->ier & IER_ETBEI) != 0U)) {
		ret = IIR_TXRDY;
	} else if(((vu->msr & MSR_DELTA_MASK) != 0U) && ((vu->ier & IER_EMSC) != 0U)) {
		ret = IIR_MLSC;
	} else {
		ret = IIR_NOPEND;
	}
	vu->iir = ret | (vu->iir & 0xF0);
	return ret;
}

/*
 * Toggle the COM port's intr pin depending on whether or not we have an
 * interrupt condition to report to the processor.
 */
void vuart_toggle_intr(struct acrn_vuart *vu)
{
	uint8_t intr_reason;

	intr_reason = vuart_intr_reason(vu);
	if (intr_reason != IIR_NOPEND) {
		vplic_accept_intr(vcpu_from_vid(vu->vm, 0), vu->irq, true);
	} else {
		vplic_accept_intr(vcpu_from_vid(vu->vm, 0), vu->irq, false);
	}
}

static bool send_to_target(struct acrn_vuart *vu, uint8_t value_u8)
{
	uint64_t rflags;
	bool ret = false;

	obtain_vuart_lock(vu, rflags);
	if (vu->active) {
		fifo_putchar(&vu->rxfifo, (char)value_u8);
		if (fifo_isfull(&vu->rxfifo)) {
			ret = true;
		}
		vuart_toggle_intr(vu);
	}
	release_vuart_lock(vu, rflags);
	return ret;
}

static uint8_t get_modem_status(uint8_t mcr)
{
	uint8_t msr;

	if ((mcr & MCR_LOOPBACK) != 0U) {
		/*
		 * In the loopback mode certain bits from the MCR are
		 * reflected back into MSR.
		 */
		msr = 0U;
		if ((mcr & MCR_RTS) != 0U) {
			msr |= MSR_CTS;
		}
		if ((mcr & MCR_DTR) != 0U) {
			msr |= MSR_DSR;
		}
		if ((mcr & MCR_OUT1) != 0U) {
			msr |= MSR_RI;
		}
		if ((mcr & MCR_OUT2) != 0U) {
			msr |= MSR_DCD;
		}
	} else {
		/*
		 * Always assert DCD and DSR so tty open doesn't block
		 * even if CLOCAL is turned off.
		 */
		msr = MSR_DCD | MSR_DSR;
	}
	return msr;
}

static uint8_t update_modem_status(uint8_t new_msr, uint8_t old_msr)
{
	uint8_t update_msr = old_msr;
	/*
	 * Detect if there has been any change between the
	 * previous and the new value of MSR. If there is
	 * then assert the appropriate MSR delta bit.
	 */
	if (((new_msr & MSR_CTS) ^ (old_msr & MSR_CTS)) != 0U) {
		update_msr |= MSR_DCTS;
	}
	if (((new_msr & MSR_DSR) ^ (old_msr & MSR_DSR)) != 0U) {
		update_msr |= MSR_DDSR;
	}
	if (((new_msr & MSR_DCD) ^ (old_msr & MSR_DCD)) != 0U) {
		update_msr |= MSR_DDCD;
	}
	if (((new_msr & MSR_RI) == 0U) && ((old_msr & MSR_RI) != 0U)) {
		update_msr |= MSR_TERI;
	}
	update_msr &= MSR_DELTA_MASK;
	update_msr |= new_msr;

	return update_msr;
}

/*
 * @pre: vu != NULL
 */
static void write_reg(struct acrn_vuart *vu, uint16_t reg, uint8_t value_u8)
{
	uint8_t msr;
	uint64_t rflags;

	obtain_vuart_lock(vu, rflags);
	/*
	 * Take care of the special case DLAB accesses first
	 */
	if (((vu->lcr & LCR_DLAB) != 0U) && (reg == UART16550_DLL)) {
		vu->dll = value_u8;
	} else if (((vu->lcr & LCR_DLAB) != 0U) && (reg == UART16550_DLM)) {
		vu->dlh = value_u8;
	} else {
		switch (reg) {
		case UART16550_THR:
			if ((vu->mcr & MCR_LOOPBACK) != 0U) {
				fifo_putchar(&vu->rxfifo, (char)value_u8);
				vu->lsr |= LSR_OE;
			} else {
				fifo_putchar(&vu->txfifo, (char)value_u8);
			}
			vu->thre_int_pending = true;
			break;
		case UART16550_IER:
			if (((vu->ier & IER_ETBEI) == 0U) && ((value_u8 & IER_ETBEI) != 0U)) {
				vu->thre_int_pending = true;
			}
			/*
			 * Apply mask so that bits 4-7 are 0
			 * Also enables bits 0-3 only if they're 1
			 */
			vu->ier = value_u8 & 0x0FU;
			break;
		case UART16550_FCR:
			/*
			 * The FCR_ENABLE bit must be '1' for the programming
			 * of other FCR bits to be effective.
			 */
			if ((value_u8 & FCR_FIFOE) == 0U) {
				vu->fcr = 0U;
			} else {
				if ((value_u8 & FCR_RFR) != 0U) {
					reset_fifo(&vu->rxfifo);
				}
				vu->fcr = value_u8 & (FCR_FIFOE | FCR_DMA | FCR_RX_MASK);
			}
			break;
		case UART16550_LCR:
			vu->lcr = value_u8;
			break;
		case UART16550_MCR:
			/* Apply mask so that bits 5-7 are 0 */
			vu->mcr = value_u8 & 0x1FU;
			msr = get_modem_status(vu->mcr);
			/*
			 * Update the value of MSR while retaining the delta
			 * bits.
			 */
			vu->msr = update_modem_status(msr, vu->msr);
			break;
		case UART16550_LSR:
			/*
			 * Line status register is not meant to be written to
			 * during normal operation.
			 */
			break;
		case UART16550_MSR:
			/*
			 * As far as I can tell MSR is a read-only register.
			 */
			break;
		case UART16550_SCR:
			vu->scr = value_u8;
			break;
		default:
			/*
			 * For the reg that is not handled (either a read-only
			 * register or an invalid register), ignore the write to it.
			 * Gracefully return if prior case clauses have not been met.
			 */
			break;
		}
	}
	vuart_toggle_intr(vu);
	release_vuart_lock(vu, rflags);
}

/*
 * @pre: vu != NULL
 */
static void vuart_write_reg(struct acrn_vuart *vu, uint16_t offset, uint8_t value_u8)
{
	struct acrn_vuart *target_vu = NULL;
	uint64_t rflags;

	target_vu = vu->target_vu;

	if (((vu->mcr & MCR_LOOPBACK) == 0U) && ((vu->lcr & LCR_DLAB) == 0U)
		&& (offset == UART16550_THR) && (target_vu != NULL)) {
		if (!send_to_target(target_vu, value_u8)) {
			/* FIFO is not full, raise THRE interrupt */
			obtain_vuart_lock(vu, rflags);
			vu->thre_int_pending = true;
			vuart_toggle_intr(vu);
			release_vuart_lock(vu, rflags);
		}
	} else {
		write_reg(vu, offset, value_u8);
	}
}

static void notify_target(const struct acrn_vuart *vu)
{
	struct acrn_vuart *t_vu;
	uint64_t rflags;

	if (vu != NULL) {
		t_vu = vu->target_vu;
		if ((t_vu != NULL) && !fifo_isfull(&vu->rxfifo)) {
			obtain_vuart_lock(t_vu, rflags);
			t_vu->thre_int_pending = true;
			vuart_toggle_intr(t_vu);
			release_vuart_lock(t_vu, rflags);
		}
	}
}

static uint8_t vuart_read_reg(struct acrn_vuart *vu, uint16_t offset)
{
	struct acrn_vuart *t_vu;
	uint8_t iir, reg = 0U, intr_reason;
	uint64_t rflags;

	t_vu = vu->target_vu;
	obtain_vuart_lock(vu, rflags);
	/*
	 * Take care of the special case DLAB accesses first
	 */
	if ((vu->lcr & LCR_DLAB) != 0U) {
		if (offset == UART16550_DLL) {
			reg = vu->dll;
		} else if (offset == UART16550_DLM) {
			reg = vu->dlh;
		} else {
			reg = 0U;
		}
	} else {
		switch (offset) {
		case UART16550_RBR:
			vu->lsr &= ~LSR_OE;
			reg = (uint8_t)fifo_getchar(&vu->rxfifo);
			break;
		case UART16550_IER:
			reg = vu->ier;
			break;
		case UART16550_IIR:
			iir = ((vu->fcr & FCR_FIFOE) != 0U) ? IIR_FIFO_MASK : 0U;
			intr_reason = vuart_intr_reason(vu);
			/*
			 * Deal with side effects of reading the IIR register
			 */
			if (intr_reason == IIR_TXRDY) {
				vu->thre_int_pending = false;
			}
			iir |= intr_reason;
			reg = iir;
			break;
		case UART16550_LCR:
			reg = vu->lcr;
			break;
		case UART16550_MCR:
			reg = vu->mcr;
			break;
		case UART16550_LSR:
			if (t_vu != NULL) {
				if (!fifo_isfull(&t_vu->rxfifo)) {
					vu->lsr |= LSR_TEMT | LSR_THRE;
				}
			} else {
				vu->lsr |= LSR_TEMT | LSR_THRE;
			}
			/* Check for new receive data */
			if (fifo_numchars(&vu->rxfifo) > 0U) {
				vu->lsr |= LSR_DR;
			} else {
				vu->lsr &= ~LSR_DR;
			}
			reg = vu->lsr;
			/* The LSR_OE bit is cleared on LSR read */
			vu->lsr &= ~LSR_OE;
			break;
		case UART16550_MSR:
			/*
			 * MSR delta bits are cleared on read
			 */
			reg = vu->msr;
			vu->msr &= ~MSR_DELTA_MASK;
			break;
		case UART16550_SCR:
			reg = vu->scr;
			break;
		default:
			reg = 0xFFU;
			break;
		}
	}
	vuart_toggle_intr(vu);
	release_vuart_lock(vu, rflags);

	/* For commnunication vuart, when the data in FIFO is read out, should
	 * notify the target vuart to send more data. */
	if (offset == UART16550_RBR) {
		notify_target(vu);
	}

	return reg;
}

static int32_t vuart_access_handler(struct io_request *io_req, void *private_data)
{
	struct acrn_vuart *vu = (struct acrn_vuart *)private_data;
	struct acrn_mmio_request *mmio = &io_req->reqs.mmio_request;
	uint32_t offset = mmio->address;

	if (vu != NULL) {
		if (mmio->direction == ACRN_IOREQ_DIR_WRITE) {
			vuart_write_reg(vu, offset,  (uint8_t)(mmio->value));
		} else {
			mmio->value = (uint64_t)vuart_read_reg(vu, offset);
		}
	}

	return 0;
}

static void setup_vuart(struct acrn_vm *vm, uint16_t vuart_idx)
{
	uint32_t divisor;
	struct acrn_vuart *vu = &vm->vuart[vuart_idx];

	/* Set baud rate*/
	divisor = (UART_CLOCK_RATE / BAUD_115200) >> 4U;
	vu->dll = (uint8_t)divisor;
	vu->dlh = (uint8_t)(divisor >> 8U);
	vu->vm = vm;
	init_fifo(vu);
	init_vuart_lock(vu);
	vu->thre_int_pending = true;
	vu->ier = 0U;
	vuart_toggle_intr(vu);
	vu->target_vu = NULL;
}

void init_vuarts(struct acrn_vm *vm, const struct vuart_config *vu_config)
{
	uint8_t i;
	struct acrn_vuart *vu;

	for (i = 0U; i < MAX_VUART_NUM_PER_VM; i++) {
		if (vu_config[i].type == VUART_MMIO) {
			vu = &vm->vuart[i];
			setup_vuart(vm, i);
			vu->base = vu_config[i].addr.base;
			vu->irq = vu_config[i].irq;
			register_mmio_emulation_handler(vm, vuart_access_handler, vu->base,
				vu->base + CONFIG_UART_SIZE, (void *)vu, false);
			vu->active = true;
		}
	}
}

void deinit_vuarts(struct acrn_vm *vm)
{
	uint8_t i;

	for (i = 0U; i < MAX_VUART_NUM_PER_VM; i++) {
		if (vm->vuart[i].active)
			vm->vuart[i].active = false;
	}
}
