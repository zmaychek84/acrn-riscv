/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app.h"

static void idle(void)
{
	asm volatile ("ecall"::);
	asm volatile ("nop"::);
//	asm volatile ("wfi"::);
}

void __userfunc guest(void)
{
	/* check if it's in U mode */
	//asm volatile ("csrr t0, sstatus"::);

	while (1) {
		idle();
	}
}
