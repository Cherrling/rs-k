/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust inode lifecycle.
 */

#ifndef _LINUX_INODE_RUST_H
#define _LINUX_INODE_RUST_H

#include <linux/fs.h>
#include <linux/list_lru.h>
#include <linux/shrinker.h>
#include <linux/types.h>

long prune_icache_sb(struct super_block *sb, struct shrink_control *sc);

#ifdef CONFIG_RUST_INODE_LIFECYCLE

void rust_helper_inode_might_sleep(void);
void rust_helper_inode_verify_lock_not_held(struct inode *inode);
void rust_helper_inode_lock(struct inode *inode);
bool rust_helper_inode_trylock(struct inode *inode);
void rust_helper_inode_unlock(struct inode *inode);

unsigned int rust_helper_inode_state_read_once(struct inode *inode);
unsigned int rust_helper_inode_state_read(struct inode *inode);
void rust_helper_inode_state_set(struct inode *inode, unsigned int flags);
void rust_helper_inode_state_clear(struct inode *inode, unsigned int flags);
void rust_helper_inode_state_replace(struct inode *inode,
				     unsigned int clearflags,
				     unsigned int setflags);
int rust_helper_inode_count_read_once(struct inode *inode);
int rust_helper_inode_count_read(struct inode *inode);
int rust_helper_inode_atomic_inc_return(struct inode *inode);
bool rust_helper_inode_atomic_add_unless_dec(struct inode *inode);
bool rust_helper_inode_atomic_dec_and_test(struct inode *inode);
int rust_helper_inode_atomic_sub_return(struct inode *inode);
void rust_helper_inode_dump(struct inode *inode);
bool rust_helper_inode_warn_once(bool condition);
bool rust_helper_inode_sb_active(struct inode *inode);
int rust_helper_inode_nlink(struct inode *inode);

bool rust_helper_inode_has_alloc(struct super_block *sb);
struct inode *rust_helper_inode_alloc_fs(struct super_block *sb);
struct inode *rust_helper_inode_alloc_slab(struct super_block *sb);
int rust_helper_inode_init_always(struct super_block *sb, struct inode *inode);
bool rust_helper_inode_has_destroy(struct inode *inode);
void rust_helper_inode_call_destroy(struct inode *inode);
bool rust_helper_inode_has_free(struct inode *inode);
void rust_helper_inode_set_free(struct inode *inode);
void rust_helper_inode_destroy_common(struct inode *inode);
void rust_helper_inode_schedule_free(struct inode *inode);
void rust_helper_inode_free_now(struct inode *inode);

bool rust_helper_inode_lru_empty(struct inode *inode);
bool rust_helper_inode_has_evict(struct inode *inode);
void rust_helper_inode_call_evict(struct inode *inode);
void rust_helper_inode_default_evict(struct inode *inode);
void rust_helper_inode_io_list_del(struct inode *inode);
void rust_helper_inode_sb_list_del(struct inode *inode);
void rust_helper_inode_wait_lru_isolating(struct inode *inode);
void rust_helper_inode_wait_writeback(struct inode *inode);
void rust_helper_inode_wake_lru_isolating(struct inode *inode);
bool rust_helper_inode_has_cdev(struct inode *inode);
void rust_helper_inode_forget_cdev(struct inode *inode);
void rust_helper_inode_remove_hash(struct inode *inode);
void rust_helper_inode_wake_new(struct inode *inode);
bool rust_helper_inode_mapping_shrinkable(struct inode *inode);
bool rust_helper_inode_mapping_empty(struct inode *inode);
unsigned long rust_helper_inode_reclaim_mapping(struct inode *inode,
						struct list_lru_one *lru);
bool rust_helper_inode_lru_add(struct inode *inode);
bool rust_helper_inode_lru_del(struct inode *inode);
void rust_helper_inode_account_unused(int delta);
bool rust_helper_inode_list_empty(struct list_head *head);
struct inode *rust_helper_inode_list_first(struct list_head *head);
void rust_helper_inode_list_del_init(struct list_head *entry);
void rust_helper_inode_list_init(struct list_head *head);
void rust_helper_inode_lru_isolate_move(struct list_lru_one *lru,
					struct inode *inode,
					struct list_head *head);
void rust_helper_inode_cond_resched(void);
long rust_helper_inode_prune_lru(struct super_block *sb,
				 struct shrink_control *sc,
				 struct list_head *freeable);

bool rust_helper_inode_has_drop(struct inode *inode);
int rust_helper_inode_call_drop(struct inode *inode);
int rust_helper_inode_generic_drop(struct inode *inode);
bool rust_helper_inode_sync_lazytime(struct inode *inode);
void rust_helper_inode_write_now(struct inode *inode);

enum lru_status rust_inode_lru_isolate(struct list_head *item,
				       struct list_lru_one *lru,
				       void *arg);
void rust_inode_destroy(struct inode *inode);
void rust_inode_lru_list_del(struct inode *inode);
void rust_inode_dispose_list(struct list_head *head);

#ifdef CONFIG_INODE_LIFECYCLE_KUNIT_TEST
void rust_inode_test_reset(void);
void rust_inode_test_fail(unsigned int stage);
unsigned int rust_inode_test_counter(unsigned int index);
#endif

#endif /* CONFIG_RUST_INODE_LIFECYCLE */

#endif /* _LINUX_INODE_RUST_H */
