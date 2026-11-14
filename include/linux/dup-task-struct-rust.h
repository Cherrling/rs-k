/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DUP_TASK_STRUCT_RUST_H
#define _LINUX_DUP_TASK_STRUCT_RUST_H

#include <linux/types.h>

struct task_struct;

#ifdef CONFIG_RUST_DUP_TASK_STRUCT

struct task_struct *
rust_dup_task_struct(struct task_struct *orig, int node);

int rust_helper_dup_task_struct_resolve_node(struct task_struct *orig, int node);
struct task_struct *rust_helper_dup_task_struct_alloc(int node);
int rust_helper_dup_task_struct_arch_copy(struct task_struct *dst,
					  struct task_struct *src);
int rust_helper_dup_task_struct_alloc_stack(struct task_struct *task, int node);
void rust_helper_dup_task_struct_account_stack(struct task_struct *task);
int rust_helper_dup_task_struct_alloc_ipi_mask(struct task_struct *task);
int rust_helper_dup_task_struct_prepare_scs(struct task_struct *task, int node);
void rust_helper_dup_task_struct_init_thread_stack(struct task_struct *task,
						   struct task_struct *orig);
void rust_helper_dup_task_struct_clear_thread_flags(struct task_struct *task);
void rust_helper_dup_task_struct_set_stack_magic(struct task_struct *task);
void rust_helper_dup_task_struct_clear_syscall_dispatch(struct task_struct *task);
void rust_helper_dup_task_struct_init_canary(struct task_struct *task);
void rust_helper_dup_task_struct_init_cpus(struct task_struct *task,
					   struct task_struct *orig, int node);
void rust_helper_dup_task_struct_init_refcounts(struct task_struct *task);
void rust_helper_dup_task_struct_init_subsystems(struct task_struct *task);
void rust_helper_dup_task_struct_init_conditional_fields(struct task_struct *task);
void rust_helper_dup_task_struct_free_ipi_mask(struct task_struct *task);
void rust_helper_dup_task_struct_exit_stack_account(struct task_struct *task);
void rust_helper_dup_task_struct_free_stack(struct task_struct *task);
void rust_helper_dup_task_struct_free_task(struct task_struct *task);

#ifdef CONFIG_DUP_TASK_STRUCT_KUNIT_TEST
#define RUST_DUP_TASK_STRUCT_TEST_FAIL_AFTER_TASK	1
#define RUST_DUP_TASK_STRUCT_TEST_FAIL_AFTER_STACK	2
#define RUST_DUP_TASK_STRUCT_TEST_FAIL_AFTER_IPI_MASK	3

u32 rust_dup_task_struct_test_entry_count(void);
u32 rust_dup_task_struct_test_task_alloc_count(void);
u32 rust_dup_task_struct_test_stack_alloc_count(void);
u32 rust_dup_task_struct_test_complete_count(void);
u32 rust_dup_task_struct_test_failure_count(void);
void rust_dup_task_struct_test_fail_next(u32 stage);
#endif

#endif /* CONFIG_RUST_DUP_TASK_STRUCT */

#endif /* _LINUX_DUP_TASK_STRUCT_RUST_H */
