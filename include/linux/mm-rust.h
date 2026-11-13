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

#ifdef CONFIG_RUST_MM_TEARDOWN

#define RUST_MM_FINAL_ACTION_DELETE_MMLIST	0x01
#define RUST_MM_FINAL_ACTION_PUT_BINFMT		0x02

void rust_helper_mm_final_check_users(struct mm_struct *mm);
void rust_helper_mm_final_uprobe_clear_state(struct mm_struct *mm);
void rust_helper_mm_final_exit_aio(struct mm_struct *mm);
void rust_helper_mm_final_ksm_exit(struct mm_struct *mm);
void rust_helper_mm_final_khugepaged_exit(struct mm_struct *mm);
void rust_helper_mm_final_exit_mmap(struct mm_struct *mm);
void rust_helper_mm_final_put_huge_zero_folio(struct mm_struct *mm);
void rust_helper_mm_final_set_exe_file_null(struct mm_struct *mm);
bool rust_helper_mm_final_mmlist_is_empty(struct mm_struct *mm);
void rust_helper_mm_final_mmlist_delete(struct mm_struct *mm);
bool rust_helper_mm_final_has_binfmt(struct mm_struct *mm);
void rust_helper_mm_final_put_binfmt(struct mm_struct *mm);
void rust_helper_mm_final_lru_gen_del_mm(struct mm_struct *mm);
void rust_helper_mm_final_futex_hash_free(struct mm_struct *mm);
void rust_helper_mm_final_drop(struct mm_struct *mm);

void rust_mm_final(struct mm_struct *mm);

#ifdef CONFIG_KUNIT
unsigned int rust_mm_final_plan_select(bool mmlist_empty, bool has_binfmt);
#endif

#endif /* CONFIG_RUST_MM_TEARDOWN */

#endif /* _LINUX_MM_RUST_H */
