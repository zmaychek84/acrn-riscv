/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/lib/bits.h>
#include <asm/cpu.h>
#include <asm/smp.h>
#include <asm/per_cpu.h>

/**
 * @pre pcpu_id < 8U
 */
void init_clint(uint16_t pcpu_id)
{
	per_cpu(swi_vector, pcpu_id).type = 0;
	per_cpu(swi_vector, pcpu_id).param = 0;
}

void
send_startup_ipi(uint16_t dest_pcpu_id, uint64_t cpu_startup_start_address)
{
}

void send_dest_ipi_mask(uint64_t dest_mask, uint64_t vector)
{
	uint16_t pcpu_id;
	uint64_t mask = dest_mask;

	pcpu_id = ffs64(mask);
	if (pcpu_id--)
		return;
	while (pcpu_id < NR_CPUS) {
		clear_bit(pcpu_id, &mask);
		if (pcpu_id != get_pcpu_id())
			send_single_swi(pcpu_id, vector);
		pcpu_id = ffs64(mask);
	}
}

void send_single_swi(uint16_t pcpu_id, uint64_t vector)
{
	unsigned long reg = CLINT_SWI_REG + pcpu_id * 4;
	int val = 0x1;

	set_bit(vector, &per_cpu(swi_vector, pcpu_id).type);
	asm volatile (
		"sw %1, 0(%0) \n\t"
		:: "r" (reg), "r" (val)
	);
}
