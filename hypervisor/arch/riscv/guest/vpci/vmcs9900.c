/*
 * Copyright (C) 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>
#include <asm/guest/vpci.h>
#include <asm/guest/instr_emul.h>
#include <logmsg.h>
#include <vmcs9900.h>
#include "vpci_priv.h"
#include <errno.h>

#define MCS9900_MMIO_BAR	0U
#define MCS9900_MSIX_BAR	1U

/*
 * @pre vdev != NULL
 */
void trigger_vmcs9900_intx(struct pci_vdev *vdev)
{
	//TODO vector should be calculated based on device BDF(swizzle algorithm)
	uint32_t vector = 0x21;
	struct acrn_vm *vm = vpci2vm(vdev->vpci);

	vplic_accept_intr(vcpu_from_vid(vm, 0), vector, true);
}

static int32_t read_vmcs9900_cfg(struct pci_vdev *vdev,
		uint32_t offset, uint32_t bytes, uint32_t * val)
{
	*val = pci_vdev_read_vcfg(vdev, offset, bytes);
	return 0;
}

int32_t vmcs9900_mmio_access_handler(struct acrn_vcpu *vcpu, uint32_t ins, uint32_t xlen)
{
	int32_t err, size;
	struct acrn_mmio_request *mmio;
	struct acrn_vuart *vu;
	struct pci_vdev *vdev;
	struct pci_vbar *vbar;
	uint16_t offset;

	size = decode_instruction(vcpu, ins, xlen);
	if (size > 0) {
		mmio = &vcpu->req.reqs.mmio_request;
		vu = &vcpu->vm->vuart[0];
		vdev = vu->vdev;
		vbar = &vdev->vbars[MCS9900_MMIO_BAR];
		offset = (uint16_t)(mmio->address - vbar->base_gpa);

		if (mmio->direction == ACRN_IOREQ_DIR_READ) {
			mmio->value = vpci_vuart_read_reg(vu, offset);
			err = emulate_instruction(vcpu, ins, xlen, size);
		} else {
			err = emulate_instruction(vcpu, ins, xlen, size);
			if (!err)
				vpci_vuart_write_reg(vu, offset, (uint8_t) mmio->value);
		}
	} else {
		pr_err("%s, unhandled access\n", __func__);
		err = -EINVAL;
	}

	return err;
}

static void map_vmcs9900_vbar(struct pci_vdev *vdev, uint32_t idx)
{
	struct acrn_vuart *vu = vdev->priv_data;
	struct acrn_vm *vm = vpci2vm(vdev->vpci);
	struct pci_vbar *vbar = &vdev->vbars[idx];

	if ((idx == MCS9900_MMIO_BAR) && (vbar->base_gpa != 0UL)) {
		vu->active = true;
	} else if ((idx == MCS9900_MSIX_BAR) && (vbar->base_gpa != 0UL)) {
		vdev->msix.mmio_gpa = vbar->base_gpa;
	} else {
		/* No action required. */
	}

}

static void unmap_vmcs9900_vbar(struct pci_vdev *vdev, uint32_t idx)
{
	struct acrn_vuart *vu = vdev->priv_data;
	struct acrn_vm *vm = vpci2vm(vdev->vpci);
	struct pci_vbar *vbar = &vdev->vbars[idx];

	if ((idx == MCS9900_MMIO_BAR) && (vbar->base_gpa != 0UL)) {
		vu->active = false;
	}
}

static int32_t write_vmcs9900_cfg(struct pci_vdev *vdev, uint32_t offset,
					uint32_t bytes, uint32_t val)
{
	if (vbar_access(vdev, offset)) {
		vpci_update_one_vbar(vdev, pci_bar_index(offset), val,
			map_vmcs9900_vbar, unmap_vmcs9900_vbar);
	} else if (msixcap_access(vdev, offset)) {
		write_vmsix_cap_reg(vdev, offset, bytes, val);
	} else {
		pci_vdev_write_vcfg(vdev, offset, bytes, val);
	}

	return 0;
}

static void init_vmcs9900(struct pci_vdev *vdev)
{
	struct acrn_vm_pci_dev_config *pci_cfg = vdev->pci_dev_config;
	struct acrn_vm *vm = vpci2vm(vdev->vpci);
	struct pci_vbar *mmio_vbar = &vdev->vbars[MCS9900_MMIO_BAR];
	struct pci_vbar *msix_vbar = &vdev->vbars[MCS9900_MSIX_BAR];
	struct acrn_vuart *vu = &vm->vuart[pci_cfg->vuart_idx];

	/* 8250-pci compartiable device */
	pci_vdev_write_vcfg(vdev, PCIR_VENDOR, 2U, MCS9900_VENDOR);
	pci_vdev_write_vcfg(vdev, PCIR_DEVICE, 2U, MCS9900_DEV);
	pci_vdev_write_vcfg(vdev, PCIR_CLASS, 1U, PCIC_SIMPLECOMM);
	pci_vdev_write_vcfg(vdev, PCIV_SUB_SYSTEM_ID, 2U, 0x1000U);
	pci_vdev_write_vcfg(vdev, PCIV_SUB_VENDOR_ID, 2U, 0xa000U);
	pci_vdev_write_vcfg(vdev, PCIR_SUBCLASS, 1U, 0x0U);
	pci_vdev_write_vcfg(vdev, PCIR_CLASS_CODE, 1U, 0x2U);

	pci_vdev_write_vcfg(vdev, PCIR_INTERRUPT_PIN, 1U, 0x1U);

	add_vmsix_capability(vdev, 1, MCS9900_MSIX_BAR);

	/* initialize vuart-pci mem bar */
	mmio_vbar->size = 0x1000U;
	mmio_vbar->base_gpa = pci_cfg->vbar_base[MCS9900_MMIO_BAR];
	mmio_vbar->mask = (uint32_t) (~(mmio_vbar->size - 1UL));
	mmio_vbar->bar_type.bits = PCIM_BAR_MEM_32;

	/* initialize vuart-pci msix bar */
	msix_vbar->size = 0x1000U;
	msix_vbar->base_gpa = pci_cfg->vbar_base[MCS9900_MSIX_BAR];
	msix_vbar->mask = (uint32_t) (~(msix_vbar->size - 1UL));
	msix_vbar->bar_type.bits = PCIM_BAR_MEM_32;

	vdev->nr_bars = 2;

	pci_vdev_write_vbar(vdev, MCS9900_MMIO_BAR, mmio_vbar->base_gpa);
	pci_vdev_write_vbar(vdev, MCS9900_MSIX_BAR, msix_vbar->base_gpa);

	/* init acrn_vuart */
	pr_info("init acrn_vuart[%d]", pci_cfg->vuart_idx);
	vdev->priv_data = vu;
	init_pci_vuart(vdev);

	vdev->user = vdev;
}

static void deinit_vmcs9900(struct pci_vdev *vdev)
{
	deinit_pci_vuart(vdev);
	vdev->user = NULL;
}

const struct pci_vdev_ops vmcs9900_ops = {
	.init_vdev = init_vmcs9900,
	.deinit_vdev = deinit_vmcs9900,
	.write_vdev_cfg = write_vmcs9900_cfg,
	.read_vdev_cfg = read_vmcs9900_cfg,
};
