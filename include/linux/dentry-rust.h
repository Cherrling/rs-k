/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust dentry lifecycle.
 */

#ifndef _LINUX_DENTRY_RUST_H
#define _LINUX_DENTRY_RUST_H

#include <linux/dcache.h>
#include <linux/list_lru.h>
#include <linux/types.h>

#ifdef CONFIG_RUST_DENTRY_LIFECYCLE

void rust_helper_dentry_might_sleep(void);
void rust_helper_dentry_lock(struct dentry *dentry);
bool rust_helper_dentry_trylock(struct dentry *dentry);
void rust_helper_dentry_lock_nested(struct dentry *dentry,
				    unsigned int subclass);
void rust_helper_dentry_unlock(struct dentry *dentry);

unsigned int rust_helper_dentry_flags(const struct dentry *dentry);
void rust_helper_dentry_set_flags(struct dentry *dentry, unsigned int flags);
int rust_helper_dentry_count(const struct dentry *dentry);
void rust_helper_dentry_dec_count(struct dentry *dentry);
bool rust_helper_dentry_dead_or_zero(const struct dentry *dentry);
bool rust_helper_dentry_is_positive(const struct dentry *dentry);
bool rust_helper_dentry_is_negative(const struct dentry *dentry);
bool rust_helper_dentry_unhashed(const struct dentry *dentry);
bool rust_helper_dentry_is_root(const struct dentry *dentry);
bool rust_helper_dentry_has_release(const struct dentry *dentry);
struct dentry *rust_helper_dentry_parent(const struct dentry *dentry);

bool rust_helper_dentry_delete(struct dentry *dentry);
void rust_helper_dentry_prune(struct dentry *dentry);
void rust_helper_dentry_release(struct dentry *dentry);
void rust_helper_dentry_verify_flags(const struct dentry *dentry,
				     unsigned int expected);
void rust_helper_dentry_verify_free(const struct dentry *dentry);
bool rust_helper_dentry_warn_once(bool condition);

void rust_helper_dentry_account_unused(int delta);
void rust_helper_dentry_account_negative(int delta);
void rust_helper_dentry_account_dentry(int delta);
bool rust_helper_dentry_lru_add(struct dentry *dentry);
bool rust_helper_dentry_lru_del(struct dentry *dentry);
void rust_helper_dentry_list_add(struct list_head *new,
				 struct list_head *head);
void rust_helper_dentry_list_del_init(struct list_head *entry);
bool rust_helper_dentry_list_empty(const struct list_head *head);
struct dentry *rust_helper_dentry_lru_tail(struct list_head *head);

void rust_helper_dentry_drop(struct dentry *dentry);
bool rust_helper_dentry_lock_for_kill(struct dentry *dentry);
void rust_helper_dentry_unlink_inode(struct dentry *dentry);
void rust_helper_dentry_unlist(struct dentry *dentry);
void rust_helper_dentry_resched_tasks_rcu_qs(void);
bool rust_helper_dentry_put_external_name(struct dentry *dentry);
void rust_helper_dentry_free(struct dentry *dentry, bool use_rcu);

enum lru_status rust_dentry_lru_isolate(struct list_head *item,
					struct list_lru_one *lru,
					void *arg);
enum lru_status rust_dentry_lru_isolate_shrink(struct list_head *item,
					       struct list_lru_one *lru,
					       void *arg);
void rust_dentry_shrink_kill(struct dentry *dentry);

#endif /* CONFIG_RUST_DENTRY_LIFECYCLE */

#endif /* _LINUX_DENTRY_RUST_H */
