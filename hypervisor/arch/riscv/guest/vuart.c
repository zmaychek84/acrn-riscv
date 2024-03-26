/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define pr_prefix		"vuart: "

#include <types.h>
#include <console.h>
#include <asm/guest/vuart.h>
#include <asm/guest/vm.h>
#include <asm/guest/instr_emul.h>
#include <logmsg.h>
#include "../uart.h"

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

	put_char(ch);
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

static void vuart_write_reg(struct acrn_vuart *vu, uint64_t offset,
			    uint64_t val, size_t size)
{
	uint64_t rflags, mask;

	obtain_vuart_lock(vu, rflags);
	for (int i = 0; i < size; i += 8) {
		fifo_putchar(&vu->rxfifo, val & 0xff);
		val >>= 8;
	}
	release_vuart_lock(vu, rflags);
}

static void vuart_write(struct acrn_vuart *vu, uint64_t offset,
			size_t size, uint64_t val)
{
	if (vu != NULL) {
		offset -= vu->port_base;
		vuart_write_reg(vu, offset, val, size);
	}
}

static uint64_t vuart_read_reg(struct acrn_vuart *vu, uint32_t offset, size_t size)
{
	uint64_t rflags;
	uint64_t val = 0;

	if (offset != 0x5)
		return val;
	obtain_vuart_lock(vu, rflags);
	for (int i = 0; i < size; i += 8) {
		uint64_t v = (uint64_t)fifo_getchar(&vu->rxfifo);
		val |= v << i;
	}
	release_vuart_lock(vu, rflags);

	return val;
}

static bool vuart_read(struct acrn_vcpu *vcpu, uint32_t offset, size_t size)
{
	struct acrn_vuart *vu = &vcpu->vm->vuart[0];
	struct acrn_mmio_request *mmio_req = &vcpu->req.reqs.mmio_request;

	if (vu != NULL) {
		offset -= vu->port_base;
		mmio_req->value = vuart_read_reg(vu, offset, size);
	}

	return true;
}

int32_t vuart_access_handler(struct acrn_vcpu *vcpu, uint32_t ins, uint32_t xlen)
{
	int32_t err;
	uint32_t offset, size;
	uint64_t qual, access_type = TYPE_INST_READ;
	struct acrn_mmio_request *mmio;
	struct acrn_vuart *vu;

	size = decode_instruction(vcpu, ins, xlen);
	qual = vcpu->arch.exit_qualification;

	if (size >= 0) {
		vu = &vcpu->vm->vuart[0];
		mmio = &vcpu->req.reqs.mmio_request;
		offset = mmio->address;
		if (mmio->direction == ACRN_IOREQ_DIR_WRITE) {
			err = emulate_instruction(vcpu, ins, xlen, size);
			if (err == 0)
				vuart_write(vu, offset, size, mmio->value);
		} else {
			(void)vuart_read(vcpu, offset, size);
			err = emulate_instruction(vcpu, ins, xlen, size);
		}
	} else {
		pr_err("%s, unhandled access\n", __func__);
		err = -EINVAL;
	}

	return err;
}

void setup_vuart(struct acrn_vm *vm, uint16_t vuart_idx)
{
	struct acrn_vuart *vu = &vm->vuart[vuart_idx];

	vu->vm = vm;
	init_fifo(vu);
	init_vuart_lock(vu);
	vu->active = true;
}
