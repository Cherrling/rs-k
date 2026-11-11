/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CPUPRI_RUST_H
#define _LINUX_CPUPRI_RUST_H

#include <linux/cpupri.h>

struct task_struct;

#ifdef CONFIG_RUST_CPUPRI

typedef bool cpupri_fitness_fn(struct task_struct *p, int cpu);

int rust_cpupri_find(struct cpupri *cp, struct task_struct *p,
		     struct cpumask *lowest_mask);
int rust_cpupri_find_fitness(struct cpupri *cp, struct task_struct *p,
			     struct cpumask *lowest_mask,
			     cpupri_fitness_fn *fitness_fn);
void rust_cpupri_set(struct cpupri *cp, int cpu, int newpri);

struct cpumask *rust_cpupri_vec_mask(struct cpupri *cp, int priority);
void rust_cpupri_warn_bad_task_priority(int priority);
void rust_cpupri_bug_bad_priority(int priority);

#endif /* CONFIG_RUST_CPUPRI */

#endif /* _LINUX_CPUPRI_RUST_H */
