/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Native Rust IRQ event entry points and narrow C helpers.
 */

#ifndef _LINUX_IRQ_EVENT_RUST_H
#define _LINUX_IRQ_EVENT_RUST_H

#include <linux/types.h>

struct irq_desc;
struct irqaction;

#ifdef CONFIG_RUST_IRQ_EVENT

/* C helpers for action access, callbacks, diagnostics, and wake primitives. */
struct irqaction *rust_irq_event_first_action(struct irq_desc *desc);
struct irqaction *rust_irq_event_next_action(struct irqaction *action);
bool rust_irq_event_action_can_thread(struct irq_desc *desc,
				      struct irqaction *action);
void rust_irq_event_mark_force_threaded(void);
u32 rust_irq_event_call_handler(struct irq_desc *desc,
				struct irqaction *action);
bool rust_irq_event_irqs_disabled(void);
void rust_irq_event_warn_irqs_enabled(struct irq_desc *desc,
				      struct irqaction *action);
void rust_irq_event_local_irq_disable(void);

bool rust_irq_event_action_has_thread(struct irqaction *action);
void rust_irq_event_warn_no_thread(struct irq_desc *desc,
				   struct irqaction *action);
bool rust_irq_event_thread_exiting(struct irqaction *action);
bool rust_irq_event_claim_thread(struct irqaction *action);
void rust_irq_event_add_oneshot(struct irq_desc *desc,
				struct irqaction *action);
void rust_irq_event_inc_thread_active(struct irq_desc *desc);
void rust_irq_event_wake_task(struct irqaction *action);

void rust_irq_event_add_randomness(struct irq_desc *desc);
bool rust_irq_event_has_debug(struct irq_desc *desc);
void rust_irq_event_note(struct irq_desc *desc, u32 action_ret);

void rust_irq_event_begin(struct irq_desc *desc);
void rust_irq_event_unlock(struct irq_desc *desc);
void rust_irq_event_lock(struct irq_desc *desc);
void rust_irq_event_finish(struct irq_desc *desc);

#endif /* CONFIG_RUST_IRQ_EVENT */

#endif /* _LINUX_IRQ_EVENT_RUST_H */
