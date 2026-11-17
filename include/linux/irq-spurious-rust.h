/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Native Rust spurious IRQ entry points and narrow C helpers.
 */

#ifndef _LINUX_IRQ_SPURIOUS_RUST_H
#define _LINUX_IRQ_SPURIOUS_RUST_H

#include <linux/types.h>

struct irq_desc;

#ifdef CONFIG_RUST_IRQ_SPURIOUS

/* Rust-owned production spurious detection state machine. */
void rust_irq_spurious_note(struct irq_desc *desc, u32 action_ret);

/* C helpers for descriptor state, time, diagnostics, and recovery actions. */
bool rust_irq_spurious_poll_in_progress(struct irq_desc *desc);
bool rust_irq_spurious_is_polled(struct irq_desc *desc);
void rust_irq_spurious_report_bad(struct irq_desc *desc, u32 action_ret);
void rust_irq_spurious_report_stuck(struct irq_desc *desc, u32 action_ret);
u32 rust_irq_spurious_threads_last(struct irq_desc *desc);
void rust_irq_spurious_set_threads_last(struct irq_desc *desc, u32 value);
u32 rust_irq_spurious_threads_handled(struct irq_desc *desc);
u32 rust_irq_spurious_unhandled(struct irq_desc *desc);
void rust_irq_spurious_set_unhandled(struct irq_desc *desc, u32 value);
bool rust_irq_spurious_window_expired(struct irq_desc *desc);
void rust_irq_spurious_mark_unhandled(struct irq_desc *desc);
u32 rust_irq_spurious_irq_count(struct irq_desc *desc);
void rust_irq_spurious_set_irq_count(struct irq_desc *desc, u32 value);
u32 rust_irq_spurious_irq(struct irq_desc *desc);
int rust_irq_spurious_fixup_level(void);
bool rust_irq_spurious_action_is_irqpoll(struct irq_desc *desc);
bool rust_irq_spurious_misrouted(struct irq_desc *desc);
void rust_irq_spurious_disable(struct irq_desc *desc);
void rust_irq_spurious_schedule_poll(void);

#endif /* CONFIG_RUST_IRQ_SPURIOUS */

#endif /* _LINUX_IRQ_SPURIOUS_RUST_H */
