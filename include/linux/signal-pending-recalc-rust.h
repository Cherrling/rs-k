/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_PENDING_RECALC_RUST_H
#define _LINUX_SIGNAL_PENDING_RECALC_RUST_H

#include <linux/types.h>

struct task_struct;

#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC

bool rust_signal_pending_recalc_tsk(struct task_struct *task);
void rust_signal_pending_recalc(struct task_struct *task);

void rust_helper_signal_pending_recalc_set(struct task_struct *task);
void rust_helper_signal_pending_recalc_clear(struct task_struct *task);
bool rust_helper_signal_pending_recalc_test(struct task_struct *task);
bool rust_helper_signal_pending_recalc_cgroup_frozen(struct task_struct *task);
bool rust_helper_signal_pending_recalc_freezing(struct task_struct *task);

#ifdef CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST
void rust_signal_pending_recalc_test_reset(void);
u32 rust_signal_pending_recalc_test_recalc_count(void);
u32 rust_signal_pending_recalc_test_tsk_count(void);
u32 rust_signal_pending_recalc_test_set_count(void);
u32 rust_signal_pending_recalc_test_freezing_count(void);
u32 rust_signal_pending_recalc_test_clear_check_count(void);
u32 rust_signal_pending_recalc_test_clear_count(void);
#endif

#endif /* CONFIG_RUST_SIGNAL_PENDING_RECALC */

#ifdef CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST
bool signal_pending_recalc_test_tsk(void);
void signal_pending_recalc_test(void);
#endif

#endif /* _LINUX_SIGNAL_PENDING_RECALC_RUST_H */
