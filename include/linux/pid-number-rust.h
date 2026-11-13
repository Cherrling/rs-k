/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_NUMBER_RUST_H
#define _LINUX_PID_NUMBER_RUST_H

#include <linux/pid_types.h>
#include <linux/types.h>

struct pid;
struct pid_namespace;
struct task_struct;

#ifdef CONFIG_RUST_PID_NUMBER

struct pid *rust_helper_pid_number_task_pid(struct task_struct *task,
					    enum pid_type type);

pid_t rust_pid_nr_ns(struct pid *pid, struct pid_namespace *ns);
pid_t rust_pid_vnr(struct pid *pid);
pid_t rust_task_pid_nr_ns(struct task_struct *task, enum pid_type type,
			  struct pid_namespace *ns);

#endif /* CONFIG_RUST_PID_NUMBER */

#endif /* _LINUX_PID_NUMBER_RUST_H */
