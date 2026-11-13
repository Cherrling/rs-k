/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TASK_RUST_H
#define _LINUX_TASK_RUST_H

struct task_struct;

#ifdef CONFIG_RUST_TASK_TEARDOWN

void rust_helper_task_final_check(struct task_struct *task);
void rust_helper_task_final_unwind_task_free(struct task_struct *task);
void rust_helper_task_final_io_uring_free(struct task_struct *task);
void rust_helper_task_final_cgroup_task_free(struct task_struct *task);
void rust_helper_task_final_task_numa_free(struct task_struct *task);
void rust_helper_task_final_security_task_free(struct task_struct *task);
void rust_helper_task_final_exit_creds(struct task_struct *task);
void rust_helper_task_final_delayacct_tsk_free(struct task_struct *task);
void rust_helper_task_final_put_signal_struct(struct task_struct *task);
void rust_helper_task_final_sched_core_free(struct task_struct *task);
void rust_helper_task_final_free_task(struct task_struct *task);

void rust_task_put_final(struct task_struct *task);

#endif /* CONFIG_RUST_TASK_TEARDOWN */

#endif /* _LINUX_TASK_RUST_H */
