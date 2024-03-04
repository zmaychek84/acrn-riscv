/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_TLB_H__
#define __RISCV_TLB_H__

#include <asm/system.h>

#define GTLB_HELPER(name)		\
static inline void name(void)		\
{					\
	asm volatile(			\
		"hfence.gvma;"		\
		: : : "memory");	\
}

/* Flush local TLBs, current VMID only. */
GTLB_HELPER(flush_guest_tlb_local);

/* Flush innershareable TLBs, current VMID only */
GTLB_HELPER(flush_guest_tlb);

/* Flush local TLBs, all VMIDs, non-hypervisor mode */
GTLB_HELPER(flush_all_guests_tlb_local);

/* Flush innershareable TLBs, all VMIDs, non-hypervisor mode */
GTLB_HELPER(flush_all_guests_tlb);

#define TLB_HELPER(name)		\
static inline void name(void)		\
{					\
	asm volatile(			\
		"sfence.vma;"		\
		: : : "memory");	\
}

/* Flush all hypervisor mappings from the TLB of the local processor. */
TLB_HELPER(flush_acrn_tlb_local);

/* Flush TLB of local processor for address va. */
static inline void  __flush_acrn_tlb_one_local(uint64_t va)
{
	asm volatile("sfence.vma;" : : "r" (va>>PAGE_SHIFT) : "memory");
}

/* Flush TLB of all processors in the inner-shareable domain for address va. */
static inline void __flush_acrn_tlb_one(uint64_t va)
{
	asm volatile("sfence.vma;" : : "r" (va>>PAGE_SHIFT) : "memory");
}

/*
 * Flush a range of VA's hypervisor mappings from the TLB of the local
 * processor.
 */
static inline void flush_acrn_tlb_range_va_local(uint64_t va, unsigned long size)
{
	uint64_t end = va + size;

	dsb(); /* Ensure preceding are visible */
	while ( va < end )
	{
		__flush_acrn_tlb_one_local(va);
		va += PAGE_SIZE;
	}
	dsb(); /* Ensure completion of the TLB flush */
	isb();
}

/*
 * Flush a range of VA's hypervisor mappings from the TLB of all
 * processors in the inner-shareable domain.
 */
static inline void flush_acrn_tlb_range_va(uint64_t va, unsigned long size)
{
	uint64_t end = va + size;

	dsb(); /* Ensure preceding are visible */
	while ( va < end )
	{
		__flush_acrn_tlb_one(va);
		va += PAGE_SIZE;
	}
	dsb(); /* Ensure completion of the TLB flush */
	isb();
}

#endif /* __RISCV_TLB_H__ */
