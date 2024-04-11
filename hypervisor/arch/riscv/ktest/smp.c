/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/smp.h>

int g_vcpus = 1;

void smp_start_cpus(int cpus)
{
	for (int i = 1; i < cpus; i++) {
		do_swi(i);
	}
}
