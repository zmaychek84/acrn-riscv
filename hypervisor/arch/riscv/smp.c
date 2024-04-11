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
	cpu *= 4;
	cpu += 0x02000000;
	asm volatile (
		"li a0, 1\n\t" \
		"sw a0, 0(%0) \n\t"
		:: "r"(cpu)
	);

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
