/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_WAKE_SELECT_RUST_H
#define _LINUX_SIGNAL_WAKE_SELECT_RUST_H

#include <linux/types.h>

struct task_struct;

#ifdef CONFIG_RUST_SIGNAL_WAKE_SELECT

void rust_signal_wake_up(struct task_struct *task, bool fatal);
void rust_ptrace_signal_wake_up(struct task_struct *task, bool resume);

#ifdef CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST
void rust_signal_wake_select_test_reset(void);
u32 rust_signal_wake_select_test_fatal_entry_count(void);
u32 rust_signal_wake_select_test_frozen_skip_count(void);
u32 rust_signal_wake_select_test_fatal_clear_count(void);
u32 rust_signal_wake_select_test_ptrace_entry_count(void);
u32 rust_signal_wake_select_test_ptrace_resume_count(void);
u32 rust_signal_wake_select_test_last_state(void);
#endif

#endif /* CONFIG_RUST_SIGNAL_WAKE_SELECT */

#endif /* _LINUX_SIGNAL_WAKE_SELECT_RUST_H */
