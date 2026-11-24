/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Native Rust IRQ resend entry points and narrow C helpers.
 */

#ifndef _LINUX_IRQ_RESEND_RUST_H
#define _LINUX_IRQ_RESEND_RUST_H

#include <linux/types.h>

struct irq_desc;
struct tasklet_struct;

#ifdef CONFIG_RUST_IRQ_RESEND

/* C helpers for descriptor fields, locks, queues, and hardware callbacks. */
bool rust_irq_resend_is_level(struct irq_desc *desc);
bool rust_irq_resend_is_nested(struct irq_desc *desc);
bool rust_irq_resend_enforces_irqctx(struct irq_desc *desc);
bool rust_irq_resend_is_pending(struct irq_desc *desc);
bool rust_irq_resend_is_replay(struct irq_desc *desc);
void rust_irq_resend_clear_pending(struct irq_desc *desc);
void rust_irq_resend_set_replay(struct irq_desc *desc);
int rust_irq_resend_parent_irq(struct irq_desc *desc);
struct irq_desc *rust_irq_resend_desc(unsigned int irq);

bool rust_irq_resend_has_direct_retrigger(struct irq_desc *desc);
int rust_irq_resend_retrigger(struct irq_desc *desc);
int rust_irq_resend_retrigger_hierarchy(struct irq_desc *desc);

void rust_irq_resend_lock(void);
void rust_irq_resend_unlock(void);
void rust_irq_resend_lock_irq(void);
void rust_irq_resend_unlock_irq(void);
bool rust_irq_resend_node_hashed(struct irq_desc *desc);
void rust_irq_resend_node_add(struct irq_desc *desc);
void rust_irq_resend_node_del(struct irq_desc *desc);
struct irq_desc *rust_irq_resend_pop(void);
void rust_irq_resend_schedule(void);
void rust_irq_resend_handle(struct irq_desc *desc);
void rust_irq_resend_init_node(struct irq_desc *desc);

#endif /* CONFIG_RUST_IRQ_RESEND */

#endif /* _LINUX_IRQ_RESEND_RUST_H */
