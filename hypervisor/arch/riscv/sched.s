/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/cpu.h>
	.text

	.align 8
	.global arch_switch_to
arch_switch_to:
	cpu_disable_irq
	cpu_ctx_save
	sd sp, 0(a0)
	ld sp, 0(a1)
	sd tp, REG_TP(sp)
	cpu_ctx_restore
	cpu_enable_irq
	ret
