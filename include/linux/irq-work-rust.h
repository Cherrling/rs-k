/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust IRQ work implementation.
 */

#ifndef _LINUX_IRQ_WORK_RUST_H
#define _LINUX_IRQ_WORK_RUST_H

#include <linux/irq_work.h>

#ifdef CONFIG_RUST_IRQ_WORK

bool rust_irq_work_queue(struct irq_work *work);
bool rust_irq_work_queue_on(struct irq_work *work, int cpu);
bool rust_irq_work_needs_cpu(void);
void rust_irq_work_single(void *arg);
void rust_irq_work_run(void);
void rust_irq_work_tick(void);
void rust_irq_work_sync(struct irq_work *work);
void rust_irq_work_run_lazy(void);

atomic_t *rust_irq_work_flags(struct irq_work *work);
struct llist_node *rust_irq_work_node(struct irq_work *work);
struct irq_work *rust_irq_work_from_node(struct llist_node *node);
struct llist_node *rust_irq_work_next_node(struct llist_node *node);
struct llist_head *rust_irq_work_list_current(bool lazy);
struct llist_head *rust_irq_work_list_cpu(int cpu, bool lazy);
bool rust_irq_work_llist_add(struct llist_node *node,
			     struct llist_head *list);
bool rust_irq_work_llist_empty(struct llist_head *list);
struct llist_node *rust_irq_work_llist_del_all(struct llist_head *list);

void rust_irq_work_preempt_disable(void);
void rust_irq_work_preempt_enable(void);
int rust_irq_work_current_cpu(void);
bool rust_irq_work_has_interrupt(void);
bool rust_irq_work_tick_stopped(void);
void rust_irq_work_raise(struct irq_work *work);
void rust_irq_work_remote_queue(int cpu, struct irq_work *work);
void rust_irq_work_warn_offline(int cpu);
void rust_irq_work_warn_in_nmi(void);
void rust_irq_work_warn_current_offline(void);
struct irq_work *rust_irq_work_remote_wakeup(int cpu);
void rust_irq_work_wake_thread(void);

void rust_irq_work_record_aux_stack(struct irq_work *work);
void rust_irq_work_lockdep_enter(int flags);
void rust_irq_work_lockdep_exit(int flags);
void rust_irq_work_smp_mb(void);
void rust_irq_work_rcuwait_wait(struct irq_work *work);
void rust_irq_work_rcuwait_wake(struct irq_work *work);
void rust_irq_work_sync_rcu(void);
void rust_irq_work_cpu_relax(void);
void rust_irq_work_assert_run_context(void);

#endif /* CONFIG_RUST_IRQ_WORK */

#endif /* _LINUX_IRQ_WORK_RUST_H */
