/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CRED_RUST_H
#define _LINUX_CRED_RUST_H

#include <linux/gfp_types.h>
#include <linux/types.h>

struct cred;
struct group_info;
struct key;
struct task_struct;
struct ucounts;
struct user_namespace;
struct user_struct;

#ifdef CONFIG_RUST_CRED_LIFECYCLE

struct cred *rust_helper_cred_alloc(gfp_t gfp);
struct cred *rust_helper_cred_alloc_zero(gfp_t gfp);
void rust_helper_cred_free(struct cred *cred);

struct task_struct *rust_helper_cred_current(void);
const struct cred *rust_helper_cred_task_objective(struct task_struct *task);
void rust_helper_cred_rcu_read_lock(void);
void rust_helper_cred_rcu_read_unlock(void);
void rust_helper_cred_publish_current(struct task_struct *task,
				      struct cred *new);
void rust_helper_cred_set_task_creds(struct task_struct *task,
				     const struct cred *new);

long rust_helper_cred_usage_read(const struct cred *cred);
void rust_helper_cred_usage_set(struct cred *cred, long usage);
void rust_helper_cred_get_many(const struct cred *cred, long nr);
bool rust_helper_cred_get_rcu(const struct cred *cred);
bool rust_helper_cred_sub_and_test(struct cred *cred, long nr);
bool rust_helper_cred_non_rcu(const struct cred *cred);
void rust_helper_cred_clear_non_rcu(struct cred *cred);

struct group_info *rust_helper_cred_group_get(struct group_info *group);
void rust_helper_cred_group_put(struct group_info *group);
u32 rust_helper_cred_group_gid(const struct group_info *group, int index);

struct user_struct *rust_helper_cred_user_get(struct user_struct *user);
void rust_helper_cred_user_put(struct user_struct *user);

struct user_namespace *
rust_helper_cred_user_ns_get(struct user_namespace *user_ns);
void rust_helper_cred_user_ns_put(struct user_namespace *user_ns);

struct ucounts *rust_helper_cred_ucounts_get(struct ucounts *ucounts);
void rust_helper_cred_ucounts_put(struct ucounts *ucounts);

struct key *rust_helper_cred_key_get(struct key *key);
void rust_helper_cred_key_put(struct key *key);
unsigned char rust_helper_cred_default_jit_keyring(void);

int rust_helper_cred_security_alloc_blank(struct cred *cred);
int rust_helper_cred_security_prepare(struct cred *new,
				      const struct cred *old);
void rust_helper_cred_security_free(struct cred *cred);
int rust_helper_cred_security_act_as(struct cred *cred, u32 secid);
int rust_helper_cred_security_create_files_as(struct cred *cred,
					      struct inode *inode);

void rust_helper_cred_invariant_failed(void) __noreturn;
bool rust_helper_cred_warn_on_once(bool condition);

void rust_helper_cred_apply_dumpability(struct task_struct *task);
void rust_helper_cred_apply_write_barrier(void);
void rust_helper_cred_key_fsuid_changed(struct cred *cred);
void rust_helper_cred_key_fsgid_changed(struct cred *cred);
void rust_helper_cred_proc_uid_changed(struct task_struct *task);
void rust_helper_cred_proc_gid_changed(struct task_struct *task);
void rust_helper_cred_switch_namespaces(const struct cred *old,
					const struct cred *new);
void rust_helper_cred_exit_keys(struct task_struct *task);
void rust_helper_cred_get_namespaces(struct task_struct *task);
void rust_helper_cred_inc_task_nproc(struct task_struct *task);
int rust_helper_cred_create_user_ns(struct cred *new);
int rust_helper_cred_install_thread_keyring(struct cred *new);

#ifdef CONFIG_CRED_LIFECYCLE_KUNIT_TEST
#define RUST_CRED_TEST_FAIL_CRED_ALLOC		1
#define RUST_CRED_TEST_FAIL_BLANK_ALLOC		2
#define RUST_CRED_TEST_FAIL_SECURITY_BLANK	3
#define RUST_CRED_TEST_FAIL_UCOUNTS_GET		4
#define RUST_CRED_TEST_FAIL_SECURITY_PREPARE	5
#define RUST_CRED_TEST_FAIL_UCOUNTS_ALLOC	6
#define RUST_CRED_TEST_FAIL_USER_NS		7

u32 rust_cred_test_entry_count(void);
u32 rust_cred_test_prepare_count(void);
u32 rust_cred_test_fork_count(void);
u32 rust_cred_test_commit_count(void);
u32 rust_cred_test_exit_count(void);
u32 rust_cred_test_release_count(void);
u32 rust_cred_test_failure_count(void);
void rust_cred_test_fail_next(u32 stage);

bool rust_cred_test_changes_dumpability(const struct cred *old,
					const struct cred *new);
bool rust_cred_test_changes_uid(const struct cred *old,
				const struct cred *new);
bool rust_cred_test_changes_gid(const struct cred *old,
				const struct cred *new);
#endif

#endif /* CONFIG_RUST_CRED_LIFECYCLE */

#endif /* _LINUX_CRED_RUST_H */
