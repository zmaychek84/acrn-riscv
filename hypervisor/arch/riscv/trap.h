/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_TRAP_H__
#define __RISCV_TRAP_H__

typedef void (* irq_handler_t)(void);
extern void hv_timer_handler(void);
#endif
