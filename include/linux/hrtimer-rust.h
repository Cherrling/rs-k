/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust hrtimer implementation.
 */

#ifndef _LINUX_HRTIMER_RUST_H
#define _LINUX_HRTIMER_RUST_H

#include <linux/hrtimer_types.h>
#include <linux/types.h>

struct hrtimer_clock_base;
struct hrtimer_cpu_base;

#ifdef CONFIG_RUST_HRTIMER

void rust_helper_hrtimer_debug_deactivate(struct hrtimer *timer);
void rust_helper_hrtimer_trace_start(struct hrtimer *timer, unsigned int mode,
				     bool was_armed);
void rust_helper_hrtimer_debug_activate(struct hrtimer *timer,
					unsigned int mode, bool was_armed);
void rust_helper_hrtimer_set_expires_range_ns(struct hrtimer *timer,
					      ktime_t expires, u64 delta_ns);
bool rust_helper_hrtimer_is_queued(struct hrtimer *timer);
void rust_helper_hrtimer_set_queued(struct hrtimer *timer, bool queued);

bool rust_hrtimer_requeue_same_base(struct hrtimer *timer,
				    struct hrtimer_clock_base *base,
				    unsigned int mode, ktime_t expires,
				    u64 delta_ns);

#endif /* CONFIG_RUST_HRTIMER */

#ifdef CONFIG_RUST_HRTIMER_ENQUEUE

void rust_helper_hrtimer_enqueue_online(struct hrtimer_cpu_base *cpu_base);

bool rust_hrtimer_enqueue(struct hrtimer *timer,
			  struct hrtimer_clock_base *base,
			  unsigned int mode, bool was_armed);

#endif /* CONFIG_RUST_HRTIMER_ENQUEUE */

#ifdef CONFIG_RUST_HRTIMER_CALLBACK

void rust_helper_hrtimer_callback_barrier(struct hrtimer_clock_base *base);
bool rust_helper_hrtimer_callback_running_matches(struct hrtimer_clock_base *base,
						  struct hrtimer *timer);

void rust_hrtimer_callback_enter(struct hrtimer *timer,
				 struct hrtimer_clock_base *base);
void rust_hrtimer_callback_exit(struct hrtimer *timer,
				struct hrtimer_clock_base *base,
				unsigned int restart);

#endif /* CONFIG_RUST_HRTIMER_CALLBACK */

#ifdef CONFIG_RUST_HRTIMER_RUN_QUEUES

void rust_helper_hrtimer_run_expired(struct hrtimer_cpu_base *cpu_base,
				     struct hrtimer_clock_base *base,
				     struct hrtimer *timer, ktime_t basenow,
				     unsigned long flags, bool sync_waiting);

void rust_hrtimer_run_queues(struct hrtimer_cpu_base *cpu_base, ktime_t now,
			     unsigned long flags, unsigned int active_mask);

#ifdef CONFIG_KUNIT
struct hrtimer *
rust_hrtimer_clock_base_next_expired(struct hrtimer_clock_base *base,
				     ktime_t basenow);
#endif

#endif /* CONFIG_RUST_HRTIMER_RUN_QUEUES */

#ifdef CONFIG_RUST_HRTIMER_INTERRUPT

ktime_t rust_helper_hrtimer_interrupt_update_base(struct hrtimer_cpu_base *cpu_base);
void rust_helper_hrtimer_interrupt_raise_softirq(void);
void rust_helper_hrtimer_interrupt_run_queues(struct hrtimer_cpu_base *cpu_base,
					      ktime_t now, unsigned long flags);
ktime_t rust_helper_hrtimer_interrupt_update_next_event(struct hrtimer_cpu_base *cpu_base);
void rust_helper_hrtimer_interrupt_program_event(ktime_t expires_next,
						 bool deferred);
bool rust_helper_hrtimer_interrupt_deferred_rearm(void);
void rust_helper_hrtimer_interrupt_defer_rearm(void);

void rust_hrtimer_interrupt(struct hrtimer_cpu_base *cpu_base,
			    unsigned long flags);

#ifdef CONFIG_KUNIT
#define RUST_HRTIMER_INTERRUPT_DONE	0
#define RUST_HRTIMER_INTERRUPT_RETRY	1
#define RUST_HRTIMER_INTERRUPT_HANG	2

int rust_hrtimer_interrupt_select(ktime_t expires_next, ktime_t now,
				  unsigned int retries,
				  unsigned int *next_retries);
int rust_hrtimer_interrupt_advance(struct hrtimer_cpu_base *cpu_base,
				   ktime_t expires_next, ktime_t now,
				   ktime_t entry_time, unsigned int retries,
				   unsigned int *next_retries);
bool rust_hrtimer_interrupt_softirq_due(ktime_t now,
					ktime_t softirq_expires_next);
ktime_t rust_hrtimer_interrupt_hang_expiry(ktime_t now,
					   unsigned int max_hang_time);
unsigned int rust_hrtimer_interrupt_test_retries(unsigned int cpu);
#endif

#endif /* CONFIG_RUST_HRTIMER_INTERRUPT */

#ifdef CONFIG_RUST_HRTIMER_START

bool rust_helper_hrtimer_keep_base(struct hrtimer *timer, bool is_local,
				   bool is_first, bool is_pinned);
struct hrtimer_clock_base *
rust_helper_hrtimer_switch_base(struct hrtimer *timer,
				struct hrtimer_clock_base *base, bool pinned);
bool rust_helper_hrtimer_enqueue_timer(struct hrtimer *timer,
				       struct hrtimer_clock_base *base,
				       unsigned int mode, bool was_armed);
void rust_helper_hrtimer_remote_reprogram(struct hrtimer_cpu_base *cpu_base);

#ifdef CONFIG_TIME_LOW_RES
ktime_t rust_helper_hrtimer_lowres_resolution(void);
#endif

int rust_hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
				u64 delta_ns, unsigned int mode,
				struct hrtimer_clock_base *base);

#ifdef CONFIG_KUNIT
#define RUST_HRTIMER_START_ACTION_NONE		0
#define RUST_HRTIMER_START_ACTION_REPROGRAM	1
#define RUST_HRTIMER_START_ACTION_FORCE		2
#define RUST_HRTIMER_START_ACTION_REMOTE_WAKE	3
#define RUST_HRTIMER_START_ACTION_DEFERRED	4

ktime_t rust_hrtimer_prepare_expiry(struct hrtimer *timer, ktime_t tim,
				    unsigned int mode, ktime_t relative_now);
int rust_hrtimer_start_reprogram_select(bool deferred, bool was_first,
					bool same_cpu, bool this_cpu_online,
					bool first, bool lazy_keep_armed);
#endif

#endif /* CONFIG_RUST_HRTIMER_START */

#if defined(CONFIG_RUST_HRTIMER_START) || \
	defined(CONFIG_RUST_HRTIMER_USER_EXPIRY)

bool rust_helper_hrtimer_remove_timer(struct hrtimer *timer,
				      struct hrtimer_clock_base *base,
				      bool newstate);
#endif

#ifdef CONFIG_RUST_HRTIMER_USER_EXPIRY

bool rust_hrtimer_check_user_timer(struct hrtimer *timer);

#ifdef CONFIG_KUNIT
bool rust_hrtimer_user_expiry_select(ktime_t soft_expires, ktime_t offset,
				     ktime_t expires_next, ktime_t now);
#endif

#endif /* CONFIG_RUST_HRTIMER_USER_EXPIRY */

#if defined(CONFIG_RUST_HRTIMER_BASE_MIGRATION) || \
	defined(CONFIG_RUST_HRTIMER_START)

struct hrtimer_cpu_base *rust_helper_hrtimer_this_cpu_base(void);
#endif

#if defined(CONFIG_RUST_HRTIMER_BASE_SELECTION) || \
	defined(CONFIG_RUST_HRTIMER_START) || \
	defined(CONFIG_RUST_HRTIMER_TARGET_BASE)

bool rust_helper_hrtimer_base_is_online(struct hrtimer_cpu_base *cpu_base);
#endif

#ifdef CONFIG_RUST_HRTIMER_BASE_SELECTION

bool rust_hrtimer_suitable_target(struct hrtimer *timer,
				  struct hrtimer_clock_base *new_base,
				  struct hrtimer_cpu_base *new_cpu_base,
				  struct hrtimer_cpu_base *this_cpu_base);

#endif /* CONFIG_RUST_HRTIMER_BASE_SELECTION */

#ifdef CONFIG_RUST_HRTIMER_TARGET_BASE

int rust_helper_hrtimer_offline_target_cpu(void);
struct hrtimer_cpu_base *rust_helper_hrtimer_percpu_base(int cpu);

struct hrtimer_cpu_base *
rust_hrtimer_target_base(struct hrtimer_cpu_base *base, bool pinned);

#ifdef CONFIG_NO_HZ_COMMON
int rust_helper_hrtimer_nohz_target_cpu(void);
#endif

#ifdef CONFIG_KUNIT
struct hrtimer_cpu_base *
rust_hrtimer_target_base_select(struct hrtimer_cpu_base *base, bool pinned,
				struct hrtimer_cpu_base *offline,
				struct hrtimer_cpu_base *nohz, bool online,
				bool migration_enabled);
#endif

#endif /* CONFIG_RUST_HRTIMER_TARGET_BASE */

#if defined(CONFIG_RUST_HRTIMER_KEEP_BASE) || \
	(defined(CONFIG_RUST_HRTIMER_TARGET_BASE) && \
	 defined(CONFIG_NO_HZ_COMMON))

bool rust_helper_hrtimer_migration_enabled(void);
#endif

#ifdef CONFIG_RUST_HRTIMER_KEEP_BASE

bool rust_helper_hrtimer_local_housekeeping(void);
bool rust_helper_hrtimer_tick_stopped(void);
bool rust_helper_hrtimer_need_resched(void);

bool rust_hrtimer_keep_base(struct hrtimer *timer, bool is_local,
			    bool is_first, bool is_pinned);

#ifdef CONFIG_KUNIT
bool rust_hrtimer_prefer_local_select(bool migration_enabled, bool is_local,
				      bool is_first, bool is_pinned,
				      bool housekeeping, bool tick_stopped,
				      bool need_resched);
#endif

#endif /* CONFIG_RUST_HRTIMER_KEEP_BASE */

#ifdef CONFIG_RUST_HRTIMER_BASE_MIGRATION

struct hrtimer_cpu_base *
rust_helper_hrtimer_target_cpu_base(struct hrtimer_cpu_base *base, bool pinned);
bool rust_helper_hrtimer_suitable_target(struct hrtimer *timer,
					 struct hrtimer_clock_base *new_base,
					 struct hrtimer_cpu_base *new_cpu_base,
					 struct hrtimer_cpu_base *this_cpu_base);
void rust_helper_hrtimer_switch_locks(struct hrtimer *timer,
				      struct hrtimer_clock_base *base,
				      struct hrtimer_clock_base *new_base);
void rust_helper_hrtimer_switch_rollback(struct hrtimer *timer,
					 struct hrtimer_clock_base *base,
					 struct hrtimer_clock_base *new_base);
void rust_helper_hrtimer_switch_commit(struct hrtimer *timer,
				       struct hrtimer_clock_base *new_base);

struct hrtimer_clock_base *
rust_hrtimer_switch_base(struct hrtimer *timer,
			 struct hrtimer_clock_base *base, bool pinned);

#ifdef CONFIG_KUNIT
struct hrtimer_clock_base *
rust_hrtimer_switch_base_to(struct hrtimer *timer,
			    struct hrtimer_clock_base *base,
			    struct hrtimer_cpu_base *this_cpu_base,
			    struct hrtimer_cpu_base *new_cpu_base);
#endif

#endif /* CONFIG_RUST_HRTIMER_BASE_MIGRATION */

#ifdef CONFIG_RUST_HRTIMER_NEXT_EVENT

ktime_t rust_hrtimer_get_next_event(struct hrtimer_cpu_base *cpu_base,
				    unsigned int active_mask);

#endif /* CONFIG_RUST_HRTIMER_NEXT_EVENT */

#ifdef CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT

ktime_t rust_hrtimer_update_next_event(struct hrtimer_cpu_base *cpu_base);

#endif /* CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT */

#ifdef CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI

bool rust_hrtimer_update_needs_ipi(struct hrtimer_cpu_base *cpu_base,
				   unsigned int active, u32 seq);

#endif /* CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI */

#ifdef CONFIG_RUST_HRTIMER_REPROGRAM

ktime_t rust_hrtimer_reprogram(struct hrtimer *timer,
			       struct hrtimer_cpu_base *cpu_base,
			       bool reprogram, bool *program_device);

#endif /* CONFIG_RUST_HRTIMER_REPROGRAM */

#if defined(CONFIG_RUST_HRTIMER_CANCEL) || \
	defined(CONFIG_RUST_HRTIMER_BASE_MIGRATION) || \
	defined(CONFIG_RUST_HRTIMER_KEEP_BASE)

bool rust_helper_hrtimer_callback_running(struct hrtimer *timer);
#endif

#ifdef CONFIG_RUST_HRTIMER_CANCEL
bool rust_helper_hrtimer_base_is_local(struct hrtimer_clock_base *base);
void rust_helper_hrtimer_force_reprogram(struct hrtimer_cpu_base *cpu_base,
					 bool skip_equal);

void rust_hrtimer_remove(struct hrtimer *timer,
			 struct hrtimer_clock_base *base, bool newstate,
			 bool reprogram);
int rust_hrtimer_try_to_cancel_locked(struct hrtimer *timer,
				      struct hrtimer_clock_base *base);

#endif /* CONFIG_RUST_HRTIMER_CANCEL */

#ifdef CONFIG_RUST_HRTIMER_CANCEL_WAIT

int rust_helper_hrtimer_try_to_cancel(struct hrtimer *timer);
void rust_helper_hrtimer_cancel_wait_running(struct hrtimer *timer);

int rust_hrtimer_cancel(struct hrtimer *timer);

#endif /* CONFIG_RUST_HRTIMER_CANCEL_WAIT */

#ifdef CONFIG_RUST_HRTIMER_FORWARD

bool rust_helper_hrtimer_forward_warn_queued(struct hrtimer *timer);
s64 rust_helper_hrtimer_forward_divns(ktime_t delta, s64 interval);
void rust_helper_hrtimer_forward_add_ns(struct hrtimer *timer, u64 delta_ns);
void rust_helper_hrtimer_forward_add(struct hrtimer *timer, ktime_t delta);

u64 rust_hrtimer_forward(struct hrtimer *timer, ktime_t now,
			 ktime_t interval);

#endif /* CONFIG_RUST_HRTIMER_FORWARD */

#endif /* _LINUX_HRTIMER_RUST_H */
