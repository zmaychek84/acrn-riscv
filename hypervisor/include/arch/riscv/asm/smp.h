/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_SMP_H__
#define __RISCV_SMP_H__

#ifndef __ASSEMBLY__
#include <asm/current.h>

struct swi_vector {
	uint32_t type;
	uint64_t param;
};
#endif /* !__ASSEMBLY__ */

#define smp_processor_id() get_pcpu_id()

extern void init_secondary(void);
extern void stop_cpu(void);
extern void smp_init_cpus(void);
extern void smp_clear_cpu_maps (void);
extern void start_pcpus(void);
extern void send_single_swi(uint16_t pcpu_id, uint32_t vector);
extern int kick_pcpu(int cpu);
extern int do_swi(int cpu);

#endif /* __RISCV_SMP_H__ */
