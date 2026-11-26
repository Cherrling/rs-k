/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OBJPOOL_RUST_H
#define _LINUX_OBJPOOL_RUST_H

#include <linux/objpool.h>

#ifdef CONFIG_RUST_OBJPOOL

void *rust_objpool_alloc_cpu_slots(size_t size, gfp_t gfp);
void *rust_objpool_alloc_slot(size_t size, int cpu, gfp_t gfp);
void rust_objpool_free_slot(void *slot);

int rust_objpool_nr_cpu_ids(void);
int rust_objpool_num_possible_cpus(void);
bool rust_objpool_cpu_possible(int cpu);

void rust_objpool_refcount_set(struct objpool_head *pool, int value);
bool rust_objpool_refcount_dec_and_test(struct objpool_head *pool);
bool rust_objpool_refcount_sub_and_test(struct objpool_head *pool, int value);

void *rust_objpool_pop(struct objpool_head *pool);

#endif /* CONFIG_RUST_OBJPOOL */

#endif /* _LINUX_OBJPOOL_RUST_H */
