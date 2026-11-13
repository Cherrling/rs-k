/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_RUST_H
#define _LINUX_PID_RUST_H

#include <linux/types.h>

struct pid;
struct pid_namespace;
struct rcu_head;
struct upid;

#ifdef CONFIG_RUST_PID_LIFECYCLE

#define RUST_PID_COUNT_ACTION_NONE		0
#define RUST_PID_COUNT_ACTION_WAKE		1
#define RUST_PID_COUNT_ACTION_WARN		2

struct upid *rust_helper_pid_upid(struct pid *pid, unsigned int level);
struct pid_namespace *rust_helper_pid_active_namespace(struct pid *pid);
void rust_helper_pid_ns_active_put(struct pid_namespace *ns);
void rust_helper_pid_wake_child_reaper(struct pid_namespace *ns);
void rust_helper_pid_warn_child_reaper(struct pid_namespace *ns);
void rust_helper_pid_idr_remove(struct pid_namespace *ns, int nr);
void rust_helper_pidfs_remove(struct pid *pid);
void rust_helper_pidfs_free(struct pid *pid);
void rust_helper_pid_cache_free(struct pid *pid, struct pid_namespace *ns);
void rust_helper_pid_call_rcu(struct pid *pid, rcu_callback_t callback);
void rust_helper_put_pid_ns(struct pid_namespace *ns);

void rust_helper_pidmap_lock(void);
void rust_helper_pidmap_unlock(void);
void rust_helper_pid_assert_tasklist_not_held(void);

void rust_pid_put(struct pid *pid);
void rust_pid_free(struct pid *pid);
void rust_pid_free_all(struct pid **pids);

#ifdef CONFIG_PID_LIFECYCLE_KUNIT_TEST
unsigned int rust_pid_test_count_action(unsigned int new_count);
unsigned int rust_pid_test_visit_order(struct pid **pids,
				       unsigned int *indices,
				       unsigned int capacity);
#endif

#endif /* CONFIG_RUST_PID_LIFECYCLE */

#endif /* _LINUX_PID_RUST_H */
