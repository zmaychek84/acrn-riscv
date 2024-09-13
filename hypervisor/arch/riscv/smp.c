/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/smp.h>
#include <asm/per_cpu.h>
#include <schedule.h>

void cpu_do_idle(void)
{
	asm volatile ("wfi"::);
}

int do_swi(int cpu)
{
	int val = 0x1;
	uint64_t off = CLINT_SWI_REG;

	off += (uint64_t)cpu * 4;
	asm volatile (
		"sw %0, 0(%1)"
		:: "r"(val), "r"(off)
		:"memory"
	);
	dsb();

	return 0;
}

int g_cpus = 1;

void cpu_dead(void)
{
	while(1);
}

int get_pcpu_nums(void)
{
	return g_cpus;
}
