/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_VPCI_H__
#define __RISCV_VPCI_H__

/* virtual PCI MMCFG address base for pre/post-launched VM. */
#define DEFAULT_VM_VIRT_PCI_MMCFG_BASE      0x30000000UL
#define DEFAULT_VM_VIRT_PCI_MMCFG_START_BUS 0x0U
#define DEFAULT_VM_VIRT_PCI_MMCFG_END_BUS   0xFFU
#define DEFAULT_VM_VIRT_PCI_MMCFG_LIMIT     0x40000000UL
#define DEFAULT_VM_VIRT_PCI_MEMBASE32       0x40000000UL
#define DEFAULT_VM_VIRT_PCI_MEMLIMIT32      0x80000000UL
#define DEFAULT_VM_VIRT_PCI_MEMBASE64       0x800000000UL   /* 32GB */
#define DEFAULT_VM_VIRT_PCI_MEMLIMIT64      0xC00000000UL   /* 48GB */

#define DEFAULT_VM_VIRT_MCS9900_MMIO_BASE   DEFAULT_VM_VIRT_PCI_MEMBASE32
#define DEFAULT_VM_VIRT_MCS9900_MSIX_BASE   (DEFAULT_VM_VIRT_MCS9900_MMIO_BASE + 0x1000)

int32_t vpci_init(struct acrn_vm *vm);
void vpci_deinit(struct acrn_vm *vm);
int32_t vpci_cfg_access_handler(struct acrn_vcpu *vcpu, uint32_t ins, uint32_t xlen);
int32_t vmcs9900_mmio_access_handler(struct acrn_vcpu *vcpu, uint32_t ins, uint32_t xlen);
int32_t vmsix_table_access_handler(struct acrn_vcpu *vcpu, uint32_t ins, uint32_t xlen);

#endif /* __RISCV_VPCI_H__ */
