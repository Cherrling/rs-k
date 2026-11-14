// SPDX-License-Identifier: GPL-2.0

#include <linux/sched/signal.h>
#include <linux/send-signal-rust.h>
#include <linux/signal.h>
#include <linux/signalfd.h>
#include <trace/events/signal.h>

__rust_helper void
rust_helper_send_signal_assert_locked(struct task_struct *task)
{
	lockdep_assert_held(&task->sighand->siglock);
}

__rust_helper bool
rust_helper_send_signal_is_noinfo(const struct kernel_siginfo *info)
{
	return info == SEND_SIG_NOINFO;
}

__rust_helper bool
rust_helper_send_signal_is_priv(const struct kernel_siginfo *info)
{
	return info == SEND_SIG_PRIV;
}

__rust_helper bool
rust_helper_send_signal_is_special(const struct kernel_siginfo *info)
{
	return info <= SEND_SIG_PRIV;
}

__rust_helper int
rust_helper_send_signal_info_code(const struct kernel_siginfo *info)
{
	return info->si_code;
}

__rust_helper bool
rust_helper_send_signal_pending_member(const struct sigpending *pending,
				       int sig)
{
	return sigismember((sigset_t *)&pending->signal, sig);
}

__rust_helper bool
rust_helper_send_signal_kernel_stop(int sig)
{
	return sig_kernel_stop(sig);
}

__rust_helper struct sigqueue *
rust_helper_send_signal_alloc_queue(int sig, struct task_struct *task,
				    int override_rlimit)
{
	return rust_send_signal_alloc_queue_bridge(sig, task, override_rlimit);
}

__rust_helper void
rust_helper_send_signal_append_queue(struct sigqueue *queue,
				     struct sigpending *pending)
{
	list_add_tail(&queue->list, &pending->list);
}

__rust_helper void
rust_helper_send_signal_init_noinfo(struct sigqueue *queue, int sig,
				    struct task_struct *task)
{
	clear_siginfo(&queue->info);
	queue->info.si_signo = sig;
	queue->info.si_errno = 0;
	queue->info.si_code = SI_USER;
	queue->info.si_pid =
		task_tgid_nr_ns(current, task_active_pid_ns(task));
	rcu_read_lock();
	queue->info.si_uid =
		from_kuid_munged(task_cred_xxx(task, user_ns), current_uid());
	rcu_read_unlock();
}

__rust_helper void
rust_helper_send_signal_init_priv(struct sigqueue *queue, int sig)
{
	clear_siginfo(&queue->info);
	queue->info.si_signo = sig;
	queue->info.si_errno = 0;
	queue->info.si_code = SI_KERNEL;
	queue->info.si_pid = 0;
	queue->info.si_uid = 0;
}

__rust_helper void
rust_helper_send_signal_copy_info(struct sigqueue *queue,
				  const struct kernel_siginfo *info)
{
	copy_siginfo(&queue->info, info);
}

__rust_helper void
rust_helper_send_signal_signalfd_notify(struct task_struct *task, int sig)
{
	signalfd_notify(task, sig);
}

__rust_helper void
rust_helper_send_signal_add_pending(sigset_t *set, int sig)
{
	sigaddset(set, sig);
}

__rust_helper void
rust_helper_send_signal_delete_stop_mask(sigset_t *set)
{
	sigdelsetmask(set, SIG_KERNEL_STOP_MASK);
}

__rust_helper void
rust_helper_send_signal_delete_cont(sigset_t *set)
{
	sigdelset(set, SIGCONT);
}

__rust_helper struct multiprocess_signals *
rust_helper_send_signal_first_delayed(struct signal_struct *signal)
{
	return hlist_entry_safe(signal->multiprocess.first,
				struct multiprocess_signals, node);
}

__rust_helper struct multiprocess_signals *
rust_helper_send_signal_next_delayed(struct multiprocess_signals *delayed)
{
	return hlist_entry_safe(delayed->node.next, struct multiprocess_signals,
				node);
}

__rust_helper bool
rust_helper_send_signal_prepare(int sig, struct task_struct *task, bool force)
{
	return rust_send_signal_prepare_bridge(sig, task, force);
}

__rust_helper void
rust_helper_send_signal_complete(int sig, struct task_struct *task,
				 unsigned int type)
{
	rust_send_signal_complete_bridge(sig, task, type);
}

__rust_helper void
rust_helper_send_signal_trace(int sig, struct kernel_siginfo *info,
			      struct task_struct *task, bool group,
			      unsigned int result)
{
	trace_signal_generate(sig, info, task, group, result);
}
