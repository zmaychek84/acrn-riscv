/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/cpu.h>
#include <asm/timer.h>
#include "uart.h"
#include "trap.h"

static int cpu_id(void)
{
	int cpu;

	asm volatile (
		"csrr a0, mhartid \n\t" \
		"mv %0, a0 \n\t" \
		:"=r"(cpu):
	);

	return cpu;
}

void m_service(struct cpu_regs *regs)
{
	int call = regs->a0;
	switch (call) {
		case 1:
			asm volatile(
				"li t0, 0x20 \n\t" \
				"csrc mip, t0 \n\t"
				::: "memory"
			);
			regs->ip += 4;
			break;
		default:
			break;
	}
}

static void mexpt_handler(void)
{
}

static void mswi_handler(void)
{
	int cpu = cpu_id();
#if 0
	char *s = "mswi_handler: d\n";

	s[14] = cpu + '0';
	early_printk(s);
#endif

	cpu *= 4;
	cpu += 0x02000000;

	asm volatile (
		"li t0, 0 \n\t" \
		"sw t0, 0(%0) \n\t" \
		"li t0, 0x2 \n\t" \
		"csrw mip, t0\n\t"
		:: "r"(cpu)
	);
}

static void mtimer_handler(void)
{
	int cpu = cpu_id();
	uint64_t val = 0x20;
	uint64_t addr = CLINT_MTIMECMP(cpu);

	asm volatile (
		"csrs mip, %0 \n\t" \
		"sw %2, 0(%1)"
		:: "r"(val), "r"(addr), "r"(CLINT_DISABLE_TIMER): "memory"
	);
#ifdef CONFIG_MACRN
	//hv_timer_handler();
#endif
}

static void mexti_handler(void)
{
}

typedef void (* irq_handler_t)(void);
static irq_handler_t mirq_handler[] = {
	mexpt_handler,
	mexpt_handler,
	mexpt_handler,
	mswi_handler,
	mexpt_handler,
	mexpt_handler,
	mexpt_handler,
	mtimer_handler,
	mexpt_handler,
	mexpt_handler,
	mexpt_handler,
	mexti_handler,
	mexpt_handler
};

void mint_handler(int irq)
{
	if (irq < 12)
		mirq_handler[irq]();
	else
		mirq_handler[12]();
}
