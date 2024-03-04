/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_MEM_H__
#define __RISCV_MEM_H__

#include <asm/config.h>

typedef uint64_t mfn_t;
typedef uint64_t paddr_t;

#define pfn_to_paddr(pfn) ((paddr_t)(pfn) << PAGE_SHIFT)
#define paddr_to_pfn(pa)  ((uint64_t)((pa) >> PAGE_SHIFT))
#define mfn_to_maddr(mfn)   pfn_to_paddr(mfn)
#define maddr_to_mfn(ma)    paddr_to_pfn(ma)
#define gfn_to_gaddr(gfn)   pfn_to_paddr(gfn)

extern void setup_pagetables(unsigned long boot_phys_offset);
extern int init_secondary_pagetables(int cpu);
extern void switch_satp(uint64_t satp);
extern void clear_fixmap_pagetable(void);

#endif /* __RISCV_MEM_H__ */
