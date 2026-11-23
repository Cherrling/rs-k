/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust list_lru implementation.
 */

#ifndef _LINUX_LIST_LRU_RUST_H
#define _LINUX_LIST_LRU_RUST_H

#include <linux/list_lru.h>

#ifdef CONFIG_RUST_LIST_LRU

struct rust_list_lru_obj_lock {
	struct list_lru_one *l;
	struct mem_cgroup *memcg;
	int nid;
	bool rcu_locked;
};

bool rust_list_lru_lock_obj(struct list_lru *lru, struct list_head *item,
			    bool irq, struct rust_list_lru_obj_lock *ctx);
void rust_list_lru_unlock_obj(struct rust_list_lru_obj_lock *ctx, bool irq);
struct list_lru_one *rust_list_lru_rcu_lock_slice(struct list_lru *lru,
						  int nid,
						  struct mem_cgroup *memcg);
void rust_list_lru_rcu_unlock(void);
unsigned long
rust_list_lru_walk_memcg(struct list_lru *lru, int nid,
			 unsigned long (*walk)(struct mem_cgroup *memcg,
					       void *arg),
			 void *arg,
			 unsigned long *nr_to_walk);

bool rust_list_lru_item_empty(const struct list_head *item);
void rust_list_lru_item_add_tail(struct list_head *item,
				 struct list_lru_one *l);
void rust_list_lru_item_del_init(struct list_head *item);
void rust_list_lru_item_move_tail(struct list_head *item,
				  struct list_lru_one *l);
void rust_list_lru_item_move(struct list_head *item, struct list_head *head);
long rust_list_lru_slice_count_read(const struct list_lru_one *l);
void rust_list_lru_slice_count_write(struct list_lru_one *l, long count);

struct list_lru_one *
rust_list_lru_walk_lock(struct list_lru *lru, int nid,
			struct mem_cgroup **memcg, bool irq, bool skip_empty);
void rust_list_lru_walk_unlock(struct list_lru_one *l, bool irq_off);

long rust_list_lru_node_count_read(const struct list_lru_node *nlru);
void rust_list_lru_node_count_inc(struct list_lru_node *nlru);
void rust_list_lru_node_count_dec(struct list_lru_node *nlru);
void rust_list_lru_set_shrinker_bit(struct mem_cgroup *memcg, int nid,
				    int shrinker_id);
int rust_list_lru_shrinker_id(struct list_lru *lru);
void __noreturn rust_list_lru_invalid_status(void);

#endif /* CONFIG_RUST_LIST_LRU */

#endif /* _LINUX_LIST_LRU_RUST_H */
