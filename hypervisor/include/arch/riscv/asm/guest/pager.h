/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_PAGER_H__
#define __RISCV_PAGER_H__

extern void init_s2pt_mem_ops(struct memory_ops *mem_ops, uint16_t vm_id);

#endif /* __RISCV_PAGER_H__ */
