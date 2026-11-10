/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal helpers for the native Rust percpu_ref() implementation.
 */

#ifndef _LINUX_PERCPU_REFCOUNT_RUST_H
#define _LINUX_PERCPU_REFCOUNT_RUST_H

#include <linux/percpu-refcount.h>

#ifdef CONFIG_RUST_PERCPU_REFCOUNT

int rust_percpu_ref_init(struct percpu_ref *ref, percpu_ref_func_t *release,
			 unsigned int flags, gfp_t gfp);
void rust_percpu_ref_exit(struct percpu_ref *ref);
void rust_percpu_ref_switch_to_atomic(struct percpu_ref *ref,
				      percpu_ref_func_t *confirm_switch);
void rust_percpu_ref_switch_to_atomic_sync(struct percpu_ref *ref);
void rust_percpu_ref_switch_to_percpu(struct percpu_ref *ref);
void rust_percpu_ref_kill_and_confirm(struct percpu_ref *ref,
				      percpu_ref_func_t *confirm_kill);
void rust_percpu_ref_resurrect(struct percpu_ref *ref);
void rust_percpu_ref_reinit(struct percpu_ref *ref);
bool rust_percpu_ref_is_zero(struct percpu_ref *ref);

int rust_percpu_ref_alloc_percpu(struct percpu_ref *ref, gfp_t gfp);
struct percpu_ref_data *rust_percpu_ref_alloc_data(gfp_t gfp);
void rust_percpu_ref_free_percpu(unsigned long percpu);
void rust_percpu_ref_free_data(struct percpu_ref_data *data);

unsigned long rust_percpu_ref_lock(void);
void rust_percpu_ref_unlock(unsigned long flags);
void rust_percpu_ref_assert_locked(void);
void rust_percpu_ref_wait_locked(struct percpu_ref *ref);
void rust_percpu_ref_wait_unlocked(struct percpu_ref *ref);
void rust_percpu_ref_wake_all(void);

unsigned long rust_percpu_ref_read_count_ptr(struct percpu_ref *ref);
void rust_percpu_ref_write_count_ptr(struct percpu_ref *ref,
				     unsigned long value);
void rust_percpu_ref_store_release_count_ptr(struct percpu_ref *ref,
					     unsigned long value);
long rust_percpu_ref_atomic_read(struct percpu_ref_data *data);
void rust_percpu_ref_atomic_set(struct percpu_ref_data *data, long value);
void rust_percpu_ref_atomic_add(struct percpu_ref_data *data, long value);
unsigned long rust_percpu_ref_sum_percpu(unsigned long percpu);
void rust_percpu_ref_zero_percpu(unsigned long percpu);
bool rust_percpu_ref_read_is_percpu(struct percpu_ref *ref,
				    unsigned long *percpu);
bool rust_percpu_ref_is_dying_inline(struct percpu_ref *ref);
void rust_percpu_ref_get(struct percpu_ref *ref);
void rust_percpu_ref_put(struct percpu_ref *ref);
void rust_percpu_ref_call_rcu_hurry(struct percpu_ref_data *data,
				    rcu_callback_t callback);
struct percpu_ref_data *
rust_percpu_ref_data_from_rcu(struct callback_head *rcu);

void rust_percpu_ref_debug_counts(struct percpu_ref_data *data,
				  unsigned long count);
void rust_percpu_ref_warn_underflow(struct percpu_ref_data *data);
bool rust_percpu_ref_warn_no_reinit(struct percpu_ref *ref);
void rust_percpu_ref_warn_confirm_switch(struct percpu_ref *ref);
void rust_percpu_ref_warn_kill_twice(struct percpu_ref *ref);
void rust_percpu_ref_warn_reinit_not_zero(bool not_zero);
void rust_percpu_ref_warn_resurrect_not_dying(struct percpu_ref *ref);
void rust_percpu_ref_warn_resurrect_percpu(struct percpu_ref *ref);
void rust_percpu_ref_warn_no_percpu(void);

#endif /* CONFIG_RUST_PERCPU_REFCOUNT */

#endif /* _LINUX_PERCPU_REFCOUNT_RUST_H */
