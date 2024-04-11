/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <irq.h>
#include <asm/init.h>
#include <asm/setup.h>
#include <asm/cache.h>
#include <asm/cpumask.h>
#include <asm/mem.h>
#include <asm/early_printk.h>
#include <asm/smp.h>
#include <asm/notify.h>
#include <asm/guest/vm.h>
#include <asm/guest/s2vm.h>
#include <debug/console.h>
#include <debug/logmsg.h>

struct bootinfo bootinfo;
size_t dcache_line_bytes;

/* C entry point for boot CPU */
void start_acrn(uint32_t cpu, unsigned long boot_phys_offset,
                      unsigned long fdt_paddr)
{
	set_current(&idle_vcpu[cpu]);
	set_pcpu_id(cpu); /* needed early, for smp_processor_id() */
	init_logmsg();

	setup_mem(boot_phys_offset);
	dcache_line_bytes = read_dcache_line_bytes();

	pr_info("start acrn, boot_phys_offset = 0x%lx\n", boot_phys_offset);
	init_trap();
	init_interrupt(BSP_CPU_ID);
	preinit_timer();
	plic_init();
//	init_pcpu_capabilities();
//	ASSERT(detect_hardware_support() == 0);

	smp_clear_cpu_maps();
	smp_init_cpus();
	start_pcpus();
	pr_info("Brought up %ld CPUs\n", (long)num_online_cpus());
	smp_call_init();

	timer_init();
	pr_info("init timer\r\n");

	console_init();
	console_setup_timer();
	pr_info("console init \r\n");
	local_irq_enable();
	setup_virt_paging();

	init_sched(0U);

	pr_info("prepare sos");
	prepare_sos_vm();
	prepare_uos_vm();

	pr_info("create vm");
	create_vm(sos_vm);
	create_vm(uos_vm);

	// currently, direct run sos.
	start_vm(sos_vm);
	start_vm(uos_vm);
	pr_info("end\n");

	run_idle_thread();
	while(1);

	asm volatile("wfi" : : : "memory");
}
