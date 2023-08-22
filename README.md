# ACRN - RISCV64

## Overview
The aim of the project is to enable ACRN for the RISCV64 architecture. This project is currently under development and is not yet ready for production. Once this support is implemented and has sufficient quality, it is intended for this port to become a part of the upstream ACRN project and to continue further development there.

The project is presented at China RISC-V summit 2023.

## Current State
The current development is based on QEMU RV64 virtual platform. The ACRN hypervisor has bootup successfully to ACRN console with the support of SMP, MMU, CLINT & PLIC. The basic virtualization support includes VM lifecycle management, vCPU context switch, memory virtualization via stage-2 page-table translation, IO virtualization via MMIO emulation, as well as IRQ virtualization with CLINT & PLIC emulation.  

Contact haicheng.li@intel.com for any further questions.

## License
This project is released under the terms of BSD-3-Clause license.
