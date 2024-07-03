/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_VUART_H__
#define __RISCV_VUART_H__

#include <asm/lib/spinlock.h>

#define	UART_MEM_ADDR		CONFIG_UART_BASE
#define	UART_MEM_REGION		UART_MEM_ADDR + CONFIG_UART_SIZE
#define UART_IRQ		(PLIC_NUM_SOURCES - 1)
#define RX_BUF_SIZE		256U
#define TX_BUF_SIZE		8192U
#define INVAILD_VUART_IDX	0xFFU

struct vuart_fifo {
	char *buf;
	uint32_t rindex;	/* index to read from */
	uint32_t windex;	/* index to write to */
	uint32_t num;		/* number of characters in the fifo */
	uint32_t size;		/* size of the fifo */
};

struct acrn_vuart {
	uint8_t data;           /* Data register (R/W) */
	uint8_t ier;            /* Interrupt enable register (R/W) */
	uint8_t iir;            /* Interrupt status register (R) */
	uint8_t lcr;            /* Line control register (R/W) */
	uint8_t mcr;            /* Modem control register (R/W) */
	uint8_t lsr;            /* Line status register (R/W) */
	uint8_t msr;            /* Modem status register (R/W) */
	uint8_t fcr;            /* FIFO control register (W) */
	uint8_t scr;            /* Scratch register (R/W) */
	uint8_t dll;            /* Baudrate divisor latch LSB */
	uint8_t dlh;            /* Baudrate divisor latch MSB */

	struct vuart_fifo rxfifo;
	struct vuart_fifo txfifo;
	uint64_t base;
	uint32_t irq;
	char vuart_rx_buf[RX_BUF_SIZE];
	char vuart_tx_buf[TX_BUF_SIZE];
	bool thre_int_pending;  /* THRE interrupt pending */
	bool active;
	struct acrn_vuart *target_vu; /* Pointer to target vuart */
	struct acrn_vm *vm;
	struct pci_vdev *vdev;  /* pci vuart */
	spinlock_t lock;	/* protects all softc elements */
};

void vuart_toggle_intr(struct acrn_vuart *vu);
#endif /* __RISCV_VUART_H__ */
