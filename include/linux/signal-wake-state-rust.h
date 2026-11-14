/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_WAKE_STATE_RUST_H
#define _LINUX_SIGNAL_WAKE_STATE_RUST_H

#include <linux/types.h>

struct task_struct;

#ifdef CONFIG_RUST_SIGNAL_WAKE_STATE

void rust_signal_wake_up_state(struct task_struct *task, unsigned int state);
void rust_helper_signal_wake_state_set_pending(struct task_struct *task);
bool rust_helper_signal_wake_state_wake_up(struct task_struct *task,
					   unsigned int state);
void rust_helper_signal_wake_state_kick(struct task_struct *task);

#ifdef CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST
void rust_signal_wake_state_test_reset(void);
u32 rust_signal_wake_state_test_entry_count(void);
u32 rust_signal_wake_state_test_set_pending_count(void);
u32 rust_signal_wake_state_test_wake_attempt_count(void);
u32 rust_signal_wake_state_test_wake_success_count(void);
u32 rust_signal_wake_state_test_kick_count(void);
u32 rust_signal_wake_state_test_last_wake_state(void);
#endif

#endif /* CONFIG_RUST_SIGNAL_WAKE_STATE */

#endif /* _LINUX_SIGNAL_WAKE_STATE_RUST_H */
