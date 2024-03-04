/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_TIMER_H__
#define __RISCV_TIMER_H__

#include <asm/cpu.h>
#include <asm/system.h>
#include <list.h>
#include <timer.h>

typedef uint64_t cycles_t;

static inline cycles_t get_cycles (void)
{
        isb();
        return cpu_csr_read(scounter);
}

extern uint64_t boot_count;

extern void udelay(uint32_t us);
extern unsigned long get_tick(void);
extern void preinit_timer(void);

#define CYCLES_PER_MS	us_to_ticks(1000U)

#define CLINT_MTIME		(CONFIG_CLINT_BASE + 0xBFF8)
#define CLINT_MTIMECMP(cpu)	(CONFIG_CLINT_BASE + 0x4000 + (cpu * 0x8))
#define CLINT_DISABLE_TIMER	0xffffffff

#endif /* __RISCV_TIMER_H__ */
