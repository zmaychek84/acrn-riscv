/*-
* Copyright (c) 2011 NetApp, Inc.
* Copyright (c) 2024 Intel Corporation.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*
* $FreeBSD$
*/

#include <errno.h>
#include <ptdev.h>
#include <asm/guest/vm.h>
#include <asm/guest/instr_emul.h>
#include <asm/guest/vpci.h>
#include <asm/io.h>
#include <logmsg.h>
#include "vpci_priv.h"
//#include <asm/pci_dev.h>
#include <hash.h>
#include <board_info.h>


static int32_t vpci_init_vdevs(struct acrn_vm *vm);
static int32_t vpci_read_cfg(struct acrn_vpci *vpci, union pci_bdf bdf, uint32_t offset, uint32_t bytes, uint32_t *val);
static int32_t vpci_write_cfg(struct acrn_vpci *vpci, union pci_bdf bdf, uint32_t offset, uint32_t bytes, uint32_t val);
static struct pci_vdev *find_available_vdev(struct acrn_vpci *vpci, union pci_bdf bdf);

/**
 * @pre io_req != NULL && private_data != NULL
 *
 * @retval 0 on success.
 * @retval other on false. (ACRN will deliver this MMIO request to DM to handle for post-launched VM)
 */
int32_t vpci_cfg_access_handler(struct acrn_vcpu *vcpu, uint32_t ins, uint32_t xlen)
{
	int32_t ret = 0, size;
	struct acrn_mmio_request *mmio;
	struct acrn_vpci *vpci;
	uint64_t pci_mmcofg_base, address;
	uint32_t reg_num;
	union pci_bdf bdf;

	size = decode_instruction(vcpu, ins, xlen);
	if (size >= 0) {
		mmio = &vcpu->req.reqs.mmio_request;
		vpci = &vcpu->vm->vpci;
		pci_mmcofg_base = vpci->pci_mmcfg.address;
		address = mmio->address;
		reg_num = (uint32_t)(address & 0xfffUL);

		/**
		 * Enhanced Configuration Address Mapping
		 * A[(20+n-1):20] Bus Number 1 ≤ n ≤ 8
		 * A[19:15] Device Number
		 * A[14:12] Function Number
		 * A[11:8] Extended Register Number
		 * A[7:2] Register Number
		 * A[1:0] Along with size of the access, used to generate Byte Enables
		 */
		bdf.value = (uint16_t)((address - pci_mmcofg_base) >> 12U);

		if (mmio->direction == ACRN_IOREQ_DIR_READ) {
			uint32_t val = ~0U;

			if (pci_is_valid_access(reg_num, (uint32_t)mmio->size)) {
				ret = vpci_read_cfg(vpci, bdf, reg_num, (uint32_t)mmio->size, &val);
			}
			mmio->value = val;

			ret = emulate_instruction(vcpu, ins, xlen, size);
		} else {
			ret = emulate_instruction(vcpu, ins, xlen, size);
			if (!ret && pci_is_valid_access(reg_num, (uint32_t)mmio->size)) {
				ret = vpci_write_cfg(vpci, bdf, reg_num, (uint32_t)mmio->size, (uint32_t)mmio->value);
			}
		}

        } else {
		pr_err("%s, unhandled access\n", __func__);
		ret = -EINVAL;
	}

	return ret;
}

/**
 * @pre vm != NULL
 * @pre vm->vm_id < CONFIG_MAX_VM_NUM
 */
int32_t vpci_init(struct acrn_vm *vm)
{
	int32_t ret = 0;

	vm->vpci.pci_mmcfg.address = DEFAULT_VM_VIRT_PCI_MMCFG_BASE;
	vm->vpci.pci_mmcfg.start_bus = DEFAULT_VM_VIRT_PCI_MMCFG_START_BUS;
	vm->vpci.pci_mmcfg.end_bus = DEFAULT_VM_VIRT_PCI_MMCFG_END_BUS;
	vm->vpci.res32.start = DEFAULT_VM_VIRT_PCI_MEMBASE32;
	vm->vpci.res32.end = DEFAULT_VM_VIRT_PCI_MEMLIMIT32;
	vm->vpci.res64.start = DEFAULT_VM_VIRT_PCI_MEMBASE64;
	vm->vpci.res64.end = DEFAULT_VM_VIRT_PCI_MEMLIMIT64;

	/* Build up vdev list for vm */
	ret = vpci_init_vdevs(vm);

	if (ret == 0) {
		spinlock_init(&vm->vpci.lock);
	}

	return ret;
}

/**
 * @pre vm != NULL
 * @pre vm->vm_id < CONFIG_MAX_VM_NUM
 */
void vpci_deinit(struct acrn_vm *vm)
{
	struct pci_vdev *vdev, *parent_vdev;
	uint32_t i;

	for (i = 0U; i < vm->vpci.pci_vdev_cnt; i++) {
		vdev = (struct pci_vdev *) &(vm->vpci.pci_vdevs[i]);

		/* Only deinit the VM's own devices */
		if (vdev->user == vdev) {
			parent_vdev = vdev->parent_user;

			vdev->vdev_ops->deinit_vdev(vdev);

			if (parent_vdev != NULL) {
				spinlock_obtain(&parent_vdev->vpci->lock);
				parent_vdev->vdev_ops->init_vdev(parent_vdev);
				spinlock_release(&parent_vdev->vpci->lock);
			}
		}
	}

	(void)memset(&vm->vpci, 0U, sizeof(struct acrn_vpci));
}

/**
 * @brief Find an available vdev structure with BDF from a specified vpci structure.
 *        If the vdev's vpci is the same as the specified vpci, the vdev is available.
 *        If the vdev's vpci is not the same as the specified vpci, the vdev has already
 *        been assigned and it is unavailable for Service VM.
 *        If the vdev's vpci is NULL, the vdev is a orphan/zombie instance, it can't
 *        be accessed by any vpci.
 *
 * @param vpci Pointer to a specified vpci structure
 * @param bdf  Indicate the vdev's BDF
 *
 * @pre vpci != NULL
 *
 * @return Return a available vdev instance, otherwise return NULL
 */
static struct pci_vdev *find_available_vdev(struct acrn_vpci *vpci, union pci_bdf bdf)
{
	struct pci_vdev *vdev = pci_find_vdev(vpci, bdf);

	if ((vdev != NULL) && (vdev->user != vdev)) {
		if (vdev->user != NULL) {
			/* the Service VM is able to access, if and only if the Service VM has higher severity than the User VM. */
			//if (get_vm_severity(vpci2vm(vpci)->vm_id) <
			//		get_vm_severity(vpci2vm(vdev->user->vpci)->vm_id)) {
			//	vdev = NULL;
			//}
		} else {
			vdev = NULL;
		}
	}

	return vdev;
}

/**
 * @pre vpci != NULL
 */
static int32_t vpci_read_cfg(struct acrn_vpci *vpci, union pci_bdf bdf,
	uint32_t offset, uint32_t bytes, uint32_t *val)
{
	int32_t ret = 0;
	struct pci_vdev *vdev;

	spinlock_obtain(&vpci->lock);
	vdev = find_available_vdev(vpci, bdf);
	if (vdev != NULL) {
		ret = vdev->vdev_ops->read_vdev_cfg(vdev, offset, bytes, val);
	} else {
		if (is_postlaunched_vm(vpci2vm(vpci))) {
			ret = -ENODEV;
		//} else if (is_plat_hidden_pdev(bdf)) {
		//	/* expose and pass through platform hidden devices */
		//	*val = pci_pdev_read_cfg(bdf, offset, bytes);
		} else {
			/* no action: e.g., PCI scan */
		}
	}
	spinlock_release(&vpci->lock);
	return ret;
}

/**
 * @pre vpci != NULL
 */
static int32_t vpci_write_cfg(struct acrn_vpci *vpci, union pci_bdf bdf,
	uint32_t offset, uint32_t bytes, uint32_t val)
{
	int32_t ret = 0;
	struct pci_vdev *vdev;

	spinlock_obtain(&vpci->lock);
	vdev = find_available_vdev(vpci, bdf);
	if (vdev != NULL) {
		ret = vdev->vdev_ops->write_vdev_cfg(vdev, offset, bytes, val);
	} else {
		if (is_postlaunched_vm(vpci2vm(vpci))) {
			ret = -ENODEV;
		//} else if (is_plat_hidden_pdev(bdf)) {
		//	/* expose and pass through platform hidden devices */
		//	pci_pdev_write_cfg(bdf, offset, bytes, val);
		} else {
			pr_acrnlog("%s %x:%x.%x not found! off: 0x%x, val: 0x%x\n", __func__,
				bdf.bits.b, bdf.bits.d, bdf.bits.f, offset, val);
		}
	}
	spinlock_release(&vpci->lock);
	return ret;
}

/**
 * @brief Initialize a vdev structure.
 *
 * The function vpci_init_vdev is used to initialize a vdev structure with a PCI device configuration(dev_config)
 * on a specified vPCI bus(vpci). If the function vpci_init_vdev initializes a SRIOV Virtual Function(VF) vdev structure,
 * the parameter parent_pf_vdev is the VF associated Physical Function(PF) vdev structure, otherwise the parameter parent_pf_vdev is NULL.
 * The caller of the function vpci_init_vdev should guarantee execution atomically.
 *
 * @param vpci              Pointer to a vpci structure
 * @param dev_config        Pointer to a dev_config structure of the vdev
 * @param parent_pf_vdev    If the parameter def_config points to a SRIOV VF vdev, this parameter parent_pf_vdev indicates the parent PF vdev.
 *                          Otherwise, it is NULL.
 *
 * @pre vpci != NULL
 * @pre vpci.pci_vdev_cnt <= CONFIG_MAX_PCI_DEV_NUM
 *
 * @return If there's a successfully initialized vdev structure return it, otherwise return NULL;
 */
struct pci_vdev *vpci_init_vdev(struct acrn_vpci *vpci, struct acrn_vm_pci_dev_config *dev_config, struct pci_vdev *parent_pf_vdev)
{
	struct pci_vdev *vdev = &vpci->pci_vdevs[vpci->pci_vdev_cnt];

	vpci->pci_vdev_cnt++;
	vdev->vpci = vpci;
	vdev->bdf.value = dev_config->vbdf.value;
	vdev->pdev = dev_config->pdev;
	vdev->pci_dev_config = dev_config;
	vdev->phyfun = parent_pf_vdev;

	hlist_add_head(&vdev->link, &vpci->vdevs_hlist_heads[hash64(dev_config->vbdf.value, VDEV_LIST_HASHBITS)]);
	if (dev_config->vdev_ops != NULL) {
		vdev->vdev_ops = dev_config->vdev_ops;
		vdev->vdev_ops->init_vdev(vdev);
	} else {
		pr_err("%s: Invalide vdev_op\n", __func__);
	}


	return vdev;
}

/**
 * @pre vm != NULL
 */
static int32_t vpci_init_vdevs(struct acrn_vm *vm)
{
	uint16_t idx;
	struct acrn_vpci *vpci = &(vm->vpci);
	const struct acrn_vm_config *vm_config = get_vm_config(vpci2vm(vpci)->vm_id);
	int32_t ret = 0;

	for (idx = 0U; idx < vm_config->pci_dev_num; idx++) {
		/* the vdev whose vBDF is unassigned will be created by hypercall */
		if ((!is_postlaunched_vm(vm)) || (vm_config->pci_devs[idx].vbdf.value != UNASSIGNED_VBDF)) {
			(void)vpci_init_vdev(vpci, &vm_config->pci_devs[idx], NULL);
		}
	}

	return ret;
}

/*
 * @pre unmap_cb != NULL
 */
void vpci_update_one_vbar(struct pci_vdev *vdev, uint32_t bar_idx, uint32_t val,
		map_pcibar map_cb, unmap_pcibar unmap_cb)
{
	struct pci_vbar *vbar = &vdev->vbars[bar_idx];
	uint32_t update_idx = bar_idx;

	if (vbar->is_mem64hi) {
		update_idx -= 1U;
	}
	unmap_cb(vdev, update_idx);
	pci_vdev_write_vbar(vdev, bar_idx, val);
	if ((map_cb != NULL) && (vdev->vbars[update_idx].base_gpa != 0UL)) {
		map_cb(vdev, update_idx);
	}
}

/**
 * @brief Add emulated legacy PCI capability support for virtual PCI device
 *
 * @param vdev     Pointer to vdev data structure
 * @param capdata  Pointer to buffer that holds the capability data to be added.
 * @param caplen   Length of buffer that holds the capability data to be added.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
uint32_t vpci_add_capability(struct pci_vdev *vdev, uint8_t *capdata, uint8_t caplen)
{
#define CAP_START_OFFSET PCI_CFG_HEADER_LENGTH

	uint8_t capoff, reallen;
	uint32_t sts;
	uint32_t ret = 0U;

	reallen = roundup(caplen, 4U); /* dword aligned */

	sts = pci_vdev_read_vcfg(vdev, PCIR_STATUS, 2U);
	if ((sts & PCIM_STATUS_CAPPRESENT) == 0U) {
		capoff = CAP_START_OFFSET;
	} else {
		capoff = vdev->free_capoff;
	}

	/* Check if we have enough space */
	if (((uint16_t)capoff + reallen) <= PCI_CONFIG_SPACE_SIZE) {
		/* Set the previous capability pointer */
		if ((sts & PCIM_STATUS_CAPPRESENT) == 0U) {
			pci_vdev_write_vcfg(vdev, PCIR_CAP_PTR, 1U, capoff);
			pci_vdev_write_vcfg(vdev, PCIR_STATUS, 2U, sts|PCIM_STATUS_CAPPRESENT);
		} else {
			pci_vdev_write_vcfg(vdev, vdev->prev_capoff + 1U, 1U, capoff);
		}

		/* Copy the capability */
		(void)memcpy_s((void *)&vdev->cfgdata.data_8[capoff], caplen, (void *)capdata, caplen);

		/* Set the next capability pointer */
		pci_vdev_write_vcfg(vdev, capoff + 1U, 1U, 0U);

		vdev->prev_capoff = capoff;
		vdev->free_capoff = capoff + reallen;
		ret = capoff;
	}

	return ret;
}
