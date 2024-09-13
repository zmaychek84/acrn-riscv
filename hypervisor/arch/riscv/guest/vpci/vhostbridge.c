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


/*_
* Emulate a PCI Host bridge:
* Generic PCI host controller targeting firmware-initialised systems and virtual machines
* Ref: Qemu hw/pci-host/gpex.c
*      Linux drivers/pci/controller/pci-host-generic.c
*/

#include <asm/guest/vm.h>
#include <pci.h>
#include "vpci_priv.h"

static void init_vhostbridge(struct pci_vdev *vdev)
{
	/* PCI config space */
	pci_vdev_write_vcfg(vdev, PCIR_VENDOR, 2U, 0x1b36U);
	pci_vdev_write_vcfg(vdev, PCIR_DEVICE, 2U, 0x0008U);
	pci_vdev_write_vcfg(vdev, PCIR_REVID, 1U, 0x0U);
	//pci_vdev_write_vcfg(vdev, PCIR_HDRTYPE, 1U, (PCIM_HDRTYPE_NORMAL | PCIM_MFDEV));
	pci_vdev_write_vcfg(vdev, PCIR_HDRTYPE, 1U, (PCIM_HDRTYPE_NORMAL));
	pci_vdev_write_vcfg(vdev, PCIR_CLASS, 1U, PCIC_BRIDGE);
	pci_vdev_write_vcfg(vdev, PCIR_SUBCLASS, 1U, PCIS_BRIDGE_HOST);

	vdev->parent_user = NULL;
	vdev->user = vdev;
}

static void deinit_vhostbridge(__unused struct pci_vdev *vdev)
{
	vdev->parent_user = NULL;
	vdev->user = NULL;
}

/**
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
static int32_t read_vhostbridge_cfg(struct pci_vdev *vdev, uint32_t offset,
	uint32_t bytes, uint32_t *val)
{
	*val = pci_vdev_read_vcfg(vdev, offset, bytes);
	return 0;
}

/**
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
static int32_t write_vhostbridge_cfg(struct pci_vdev *vdev, uint32_t offset,
	uint32_t bytes, uint32_t val)
{
	if (!is_bar_offset(PCI_BAR_COUNT, offset)) {
		pci_vdev_write_vcfg(vdev, offset, bytes, val);
	}
	return 0;
}

const struct pci_vdev_ops vhostbridge_ops = {
	.init_vdev	= init_vhostbridge,
	.deinit_vdev	= deinit_vhostbridge,
	.write_vdev_cfg	= write_vhostbridge_cfg,
	.read_vdev_cfg	= read_vhostbridge_cfg,
};
