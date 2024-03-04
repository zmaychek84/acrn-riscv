#ifndef __RISCV_TRAP_H__
#define __RISCV_TRAP_H__

typedef void (* irq_handler_t)(void);
extern void dispatch_interrupt(struct cpu_regs *regs);
extern void hv_timer_handler(void);
#endif
