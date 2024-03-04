/*
 * Copyright (C) 2023 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_APICREG_H__
#define __RISCV_APICREG_H__

#include <asm/page.h>

#define CLINT_RSV0 ((0x4000 - 0x10) >> 2)
#define CLINT_RSV1 ((0xBFF8 - 0x4020) >> 3)

struct clint_regs {
	uint32_t msip[5];
	uint32_t rsv0[CLINT_RSV0];
	uint64_t mtimer[5];
	uint64_t rsv1[CLINT_RSV1];
	uint64_t mtime;
	uint64_t rsv2;
} __aligned(PAGE_SIZE);

enum CLINT_REGISTERS {
	MSIP0		= 0x0,
	MSIP1	 	= 0x4,
	MSIP2	 	= 0x8,
	MSIP3	 	= 0xC,
	MSIP4	 	= 0x10,
	MTIMER0 	= 0x4000,
	MTIMER1 	= 0x4008,
	MTIMER2 	= 0x4010,
	MTIMER3 	= 0x4018,
	MTIMER4 	= 0x4020,
	MTIME	 	= 0xBFF8,
};

#define	CLINT_MEM_ADDR		CONFIG_CLINT_BASE
#define	CLINT_MEM_REGION	CLINT_MEM_ADDR + CONFIG_CLINT_SIZE

#define DEFAULT_CLINT_BASE	0x2000000UL
#define DEFAULT_CLINT_SIZE	0xC000UL

/* LVT table indices */
#define	CLINT_LVT_MSIP0		0U
#define	CLINT_LVT_MSIP1		1U
#define	CLINT_LVT_MSIP2		2U
#define	CLINT_LVT_MSIP3		3U
#define	CLINT_LVT_MSIP4		4U
#define	CLINT_LVT_MAX		CLINT_LVT_MSIP4
#define	CLINT_LVT_ERROR		0xFFU

#define MAX_RTE_SHIFT		16U

#define CLINT_VECTOR_SSI       0x00000002U
#define CLINT_VECTOR_STI       0x00000020U
#define CLINT_VECTOR_SEI       0x00000200U

#endif /* __RISCV_APICREG_H__ */
