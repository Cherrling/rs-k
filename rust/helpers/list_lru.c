// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bug.h>
#include <linux/list_lru-rust.h>
#include <linux/memcontrol.h>

#ifdef CONFIG_RUST_LIST_LRU

bool rust_list_lru_item_empty(const struct list_head *item)
{
	return list_empty(item);
}

void rust_list_lru_item_add_tail(struct list_head *item,
				 struct list_lru_one *l)
{
	list_add_tail(item, &l->list);
}

void rust_list_lru_item_del_init(struct list_head *item)
{
	list_del_init(item);
}

void rust_list_lru_item_move_tail(struct list_head *item,
				  struct list_lru_one *l)
{
	list_move_tail(item, &l->list);
}

void rust_list_lru_item_move(struct list_head *item, struct list_head *head)
{
	list_move(item, head);
}

long rust_list_lru_slice_count_read(const struct list_lru_one *l)
{
	return READ_ONCE(l->nr_items);
}

void rust_list_lru_slice_count_write(struct list_lru_one *l, long count)
{
	WRITE_ONCE(l->nr_items, count);
}

long rust_list_lru_node_count_read(const struct list_lru_node *nlru)
{
	return atomic_long_read(&nlru->nr_items);
}

void rust_list_lru_node_count_inc(struct list_lru_node *nlru)
{
	atomic_long_inc(&nlru->nr_items);
}

void rust_list_lru_node_count_dec(struct list_lru_node *nlru)
{
	atomic_long_dec(&nlru->nr_items);
}

void rust_list_lru_set_shrinker_bit(struct mem_cgroup *memcg, int nid,
				    int shrinker_id)
{
	set_shrinker_bit(memcg, nid, shrinker_id);
}

int rust_list_lru_shrinker_id(struct list_lru *lru)
{
#ifdef CONFIG_MEMCG
	return lru->shrinker_id;
#else
	return -1;
#endif
}

void __noreturn rust_list_lru_invalid_status(void)
{
	BUG();
}

#endif /* CONFIG_RUST_LIST_LRU */
