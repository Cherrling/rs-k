/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CPUPRI_RUST_H
#define _LINUX_CPUPRI_RUST_H

#include <linux/cpupri.h>

struct task_struct;

#ifdef CONFIG_RUST_CPUPRI

typedef bool cpupri_fitness_fn(struct task_struct *p, int cpu);

struct cpumask *rust_cpupri_vec_mask(struct cpupri *cp, int priority);
void rust_cpupri_warn_bad_task_priority(int priority);
void rust_cpupri_bug_bad_priority(int priority);

#endif /* CONFIG_RUST_CPUPRI */

#endif /* _LINUX_CPUPRI_RUST_H */
