/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NSPROXY_RUST_H
#define _LINUX_NSPROXY_RUST_H

#include <linux/types.h>

struct cgroup_namespace;
struct cred;
struct fs_struct;
struct ipc_namespace;
struct mnt_namespace;
struct net;
struct nsproxy;
struct pid_namespace;
struct task_struct;
struct time_namespace;
struct user_namespace;
struct uts_namespace;

#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE

struct nsproxy *create_new_namespaces(u64 flags, struct task_struct *task,
				      struct user_namespace *user_ns,
				      struct fs_struct *new_fs);

struct nsproxy *rust_helper_nsproxy_alloc(void);
void rust_helper_nsproxy_free_object(struct nsproxy *ns);
void rust_helper_nsproxy_get(struct nsproxy *ns);
void rust_helper_nsproxy_put(struct nsproxy *ns);
void rust_helper_nsproxy_active_get(struct nsproxy *ns);
void rust_helper_nsproxy_active_put(struct nsproxy *ns);

u64 rust_helper_nsproxy_nonuser_flags(void);
u64 rust_helper_nsproxy_empty_mntns_flag(void);
u64 rust_helper_nsproxy_clone_empty_mntns_flag(void);

struct task_struct *rust_helper_nsproxy_current(void);
struct nsproxy *rust_helper_nsproxy_task(const struct task_struct *task);
void rust_helper_nsproxy_set_task(struct task_struct *task,
				  struct nsproxy *ns);
struct nsproxy *rust_helper_nsproxy_swap_task(struct task_struct *task,
					      struct nsproxy *new);
struct user_namespace *
rust_helper_nsproxy_task_user_ns(struct task_struct *task);
struct user_namespace *
rust_helper_nsproxy_cred_user_ns(const struct cred *cred);
struct user_namespace *rust_helper_nsproxy_current_user_ns(void);
struct fs_struct *rust_helper_nsproxy_task_fs(struct task_struct *task);
struct fs_struct *rust_helper_nsproxy_current_fs(void);
bool rust_helper_nsproxy_capable(struct user_namespace *user_ns);
void rust_helper_nsproxy_might_sleep(void);

struct mnt_namespace *
rust_helper_nsproxy_copy_mnt(u64 flags, struct mnt_namespace *old,
			     struct user_namespace *user_ns,
			     struct fs_struct *new_fs);
struct uts_namespace *
rust_helper_nsproxy_copy_uts(u64 flags, struct uts_namespace *old,
			     struct user_namespace *user_ns);
struct ipc_namespace *
rust_helper_nsproxy_copy_ipc(u64 flags, struct ipc_namespace *old,
			     struct user_namespace *user_ns);
struct pid_namespace *
rust_helper_nsproxy_copy_pid(u64 flags, struct pid_namespace *old,
			     struct user_namespace *user_ns);
struct cgroup_namespace *
rust_helper_nsproxy_copy_cgroup(u64 flags, struct cgroup_namespace *old,
				struct user_namespace *user_ns);
struct net *
rust_helper_nsproxy_copy_net(u64 flags, struct net *old,
			     struct user_namespace *user_ns);
struct time_namespace *
rust_helper_nsproxy_copy_time(u64 flags, struct time_namespace *old,
			      struct user_namespace *user_ns);
struct time_namespace *
rust_helper_nsproxy_get_time(struct time_namespace *ns);

struct mnt_namespace *rust_helper_nsproxy_mnt(const struct nsproxy *ns);
struct uts_namespace *rust_helper_nsproxy_uts(const struct nsproxy *ns);
struct ipc_namespace *rust_helper_nsproxy_ipc(const struct nsproxy *ns);
struct pid_namespace *rust_helper_nsproxy_pid(const struct nsproxy *ns);
struct cgroup_namespace *
rust_helper_nsproxy_cgroup(const struct nsproxy *ns);
struct net *rust_helper_nsproxy_net(const struct nsproxy *ns);
struct time_namespace *
rust_helper_nsproxy_time(const struct nsproxy *ns);
struct time_namespace *
rust_helper_nsproxy_time_for_children(const struct nsproxy *ns);

void rust_helper_nsproxy_set_mnt(struct nsproxy *ns, struct mnt_namespace *mnt);
void rust_helper_nsproxy_set_uts(struct nsproxy *ns, struct uts_namespace *uts);
void rust_helper_nsproxy_set_ipc(struct nsproxy *ns, struct ipc_namespace *ipc);
void rust_helper_nsproxy_set_pid(struct nsproxy *ns, struct pid_namespace *pid);
void rust_helper_nsproxy_set_cgroup(struct nsproxy *ns,
				    struct cgroup_namespace *cgroup);
void rust_helper_nsproxy_set_net(struct nsproxy *ns, struct net *net);
void rust_helper_nsproxy_set_time(struct nsproxy *ns,
				  struct time_namespace *time);
void rust_helper_nsproxy_set_time_for_children(struct nsproxy *ns,
					       struct time_namespace *time);

void rust_helper_nsproxy_put_mnt(struct mnt_namespace *ns);
void rust_helper_nsproxy_put_uts(struct uts_namespace *ns);
void rust_helper_nsproxy_put_ipc(struct ipc_namespace *ns);
void rust_helper_nsproxy_put_pid(struct pid_namespace *ns);
void rust_helper_nsproxy_put_cgroup(struct cgroup_namespace *ns);
void rust_helper_nsproxy_put_net(struct net *ns);
void rust_helper_nsproxy_put_time(struct time_namespace *ns);

void rust_helper_nsproxy_user_ns_active_get(struct user_namespace *ns);
void rust_helper_nsproxy_user_ns_active_put(struct user_namespace *ns);
bool rust_helper_nsproxy_time_equal(const struct nsproxy *ns);
void rust_helper_nsproxy_timens_on_fork(struct nsproxy *ns,
					struct task_struct *task);

bool rust_helper_nsproxy_is_err(const void *ptr);
long rust_helper_nsproxy_ptr_err(const void *ptr);
void *rust_helper_nsproxy_err_ptr(long error);

#ifdef CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST
#define RUST_NSPROXY_TEST_FAIL_ALLOC		1
#define RUST_NSPROXY_TEST_FAIL_MNT		2
#define RUST_NSPROXY_TEST_FAIL_UTS		3
#define RUST_NSPROXY_TEST_FAIL_IPC		4
#define RUST_NSPROXY_TEST_FAIL_PID		5
#define RUST_NSPROXY_TEST_FAIL_CGROUP		6
#define RUST_NSPROXY_TEST_FAIL_NET		7
#define RUST_NSPROXY_TEST_FAIL_TIME		8

u32 rust_nsproxy_test_create_count(void);
u32 rust_nsproxy_test_copy_count(void);
u32 rust_nsproxy_test_switch_count(void);
u32 rust_nsproxy_test_free_count(void);
u32 rust_nsproxy_test_release_count(void);
u32 rust_nsproxy_test_failure_count(void);
void rust_nsproxy_test_fail_next(u32 stage);
#endif

#endif /* CONFIG_RUST_NSPROXY_LIFECYCLE */

#ifdef CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST
struct nsproxy *nsproxy_test_create(void);
#endif

#endif /* _LINUX_NSPROXY_RUST_H */
