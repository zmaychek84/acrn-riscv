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
	struct vuart_fifo rxfifo;
	struct vuart_fifo txfifo;
	uint16_t port_base;
	char vuart_rx_buf[RX_BUF_SIZE];
	char vuart_tx_buf[TX_BUF_SIZE];
	struct acrn_vm *vm;
	spinlock_t lock;	/* protects all softc elements */
	bool active;
};

static inline void vuart_toggle_intr(const struct acrn_vuart *vu) {}
#endif /* __RISCV_VUART_H__ */
