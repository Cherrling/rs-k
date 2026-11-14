/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TASK_GROUP_EXIT_SYNC_RUST_H
#define _LINUX_TASK_GROUP_EXIT_SYNC_RUST_H

#include <linux/types.h>

struct core_state;
struct sighand_struct;
struct task_struct;

#ifdef CONFIG_RUST_TASK_GROUP_EXIT_SYNC

void rust_task_group_exit_sync(struct task_struct *task, long code);

struct sighand_struct *
rust_helper_task_group_exit_sync_lock(struct task_struct *task);
void
rust_helper_task_group_exit_sync_unlock(struct sighand_struct *sighand);
void
rust_helper_task_group_exit_sync_coredump(struct task_struct *task,
					  struct core_state *core_state);

#ifdef CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST
#define RUST_TASK_GROUP_EXIT_SYNC_KUNIT_CODE 73

bool rust_task_group_exit_sync_test_should_start(int quick_threads,
						 bool group_exit);
bool
rust_task_group_exit_sync_test_should_enter_coredump(bool core_state_present);
u32 rust_task_group_exit_sync_test_entry_count(void);
u32 rust_task_group_exit_sync_test_group_exit_count(void);
u32 rust_task_group_exit_sync_test_pending_count(void);
u32 rust_task_group_exit_sync_test_last_group_exit_flags(void);
int rust_task_group_exit_sync_test_last_group_exit_code(void);
int rust_task_group_exit_sync_test_last_group_stop_count(void);
u32 rust_task_group_exit_sync_test_post_coredump_count(void);
#endif

#endif /* CONFIG_RUST_TASK_GROUP_EXIT_SYNC */

#endif /* _LINUX_TASK_GROUP_EXIT_SYNC_RUST_H */
