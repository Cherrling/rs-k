/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_RUST_H
#define _LINUX_MM_RUST_H

#include <linux/types.h>

struct mm_struct;
struct task_struct;

#ifdef CONFIG_RUST_MM_LIFETIME

#define RUST_MM_GET_ACTION_NONE			0x00
#define RUST_MM_GET_ACTION_ACQUIRE		0x01

#define RUST_MM_PUT_ACTION_RETURN		0x01
#define RUST_MM_PUT_ACTION_FINALIZE		0x02
#define RUST_MM_PUT_ACTION_ASYNC		0x04

void *rust_helper_mm_users_ptr(struct mm_struct *mm);
bool rust_helper_mm_task_is_kernel_thread(struct task_struct *task);
void rust_helper_mm_task_lock(struct task_struct *task);
void rust_helper_mm_task_unlock(struct task_struct *task);
struct mm_struct *rust_helper_mm_task_mm(struct task_struct *task);
void rust_helper_mm_put_final(struct mm_struct *mm);
void rust_helper_mm_put_async_schedule(struct mm_struct *mm);

struct mm_struct *rust_mm_get_task(struct task_struct *task);
void rust_mm_put(struct mm_struct *mm);
void rust_mm_put_async(struct mm_struct *mm);

#ifdef CONFIG_KUNIT
unsigned int rust_mm_get_task_plan_select(bool kernel_thread, bool has_mm);
unsigned int rust_mm_put_plan_select(bool became_zero, bool async);
#endif

#endif /* CONFIG_RUST_MM_LIFETIME */

#endif /* _LINUX_MM_RUST_H */
