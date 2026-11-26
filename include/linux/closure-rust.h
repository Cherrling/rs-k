/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CLOSURE_RUST_H
#define _LINUX_CLOSURE_RUST_H

#include <linux/closure.h>

#ifdef CONFIG_RUST_CLOSURE

struct closure_syncer {
	struct task_struct *task;
	int done;
};

int rust_closure_atomic_read(struct closure *cl);
void rust_closure_atomic_set(struct closure *cl, int value);
void rust_closure_atomic_add(struct closure *cl, int value);
int rust_closure_atomic_sub_return_release(struct closure *cl, int value);
int rust_closure_atomic_dec_return_release(struct closure *cl);
int rust_closure_atomic_cmpxchg(struct closure *cl, int old, int new);

void rust_closure_acquire_after_ctrl_dep(void);
void rust_closure_put_after_sub_checks(int flags);
void rust_closure_queue(struct closure *cl);
void rust_closure_set_ip(struct closure *cl);
void rust_closure_set_waiting(struct closure *cl, unsigned long ip);

struct llist_node *rust_closure_llist_del_all(struct closure_waitlist *list);
bool rust_closure_llist_add(struct closure *cl, struct closure_waitlist *list);

void rust_closure_set_current_state(unsigned int state);
void rust_closure_set_current_running(void);
void rust_closure_schedule(void);
long rust_closure_schedule_timeout(long timeout);

#ifdef CONFIG_DEBUG_CLOSURES
void rust_closure_debug_register(struct closure *cl);
void rust_closure_debug_unregister(struct closure *cl);
#endif

#endif /* CONFIG_RUST_CLOSURE */

#endif /* _LINUX_CLOSURE_RUST_H */
