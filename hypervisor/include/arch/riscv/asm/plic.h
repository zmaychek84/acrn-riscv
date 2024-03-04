/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_PLIC_H__
#define __RISCV_PLIC_H__

#include <types.h>
#include <asm/lib/spinlock.h>
#include <acrn/config.h>
#include <irq.h>

#define PLIC_IPRR	(0x0000)
#define PLIC_IPER	(0x1000)
#define PLIC_IER	(0x2000)
#define PLIC_THR	(0x200000)
#define PLIC_EOIR	(0x200004)

#define PLIC_IRQ_MASK 	(0xFFFFFFFE)

struct acrn_plic {
	spinlock_t lock;
	paddr_t base;
	void *map_base;
	uint32_t size;
};

#endif /* __RISCV_PLIC_H__ */
