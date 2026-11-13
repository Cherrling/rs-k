/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_ALLOCATION_RUST_H
#define _LINUX_PID_ALLOCATION_RUST_H

#include <linux/pid_types.h>
#include <linux/types.h>

struct pid;
struct pid_namespace;
struct upid;

#ifdef CONFIG_RUST_PID_ALLOCATION

struct pid *rust_helper_pid_alloc_cache_alloc(struct pid_namespace *ns);
void rust_helper_pid_alloc_cache_free(struct pid *pid,
				      struct pid_namespace *ns);
void rust_helper_pid_alloc_get_ns(struct pid_namespace *ns);
void rust_helper_pid_alloc_put_ns(struct pid_namespace *ns);
void rust_helper_pid_alloc_init(struct pid *pid);

int rust_helper_pid_alloc_pid_max(struct pid_namespace *ns);
bool rust_helper_pid_alloc_capable(struct pid_namespace *ns);
bool rust_helper_pid_alloc_child_reaper_missing(struct pid_namespace *ns);

unsigned int rust_helper_pid_alloc_idr_cursor(struct pid_namespace *ns);
void rust_helper_pid_alloc_idr_set_cursor(struct pid_namespace *ns,
					  unsigned int cursor);
void rust_helper_pid_alloc_idr_preload(void);
void rust_helper_pid_alloc_idr_preload_end(void);
int rust_helper_pid_alloc_idr_alloc(struct pid_namespace *ns,
				    int start, int end);
int rust_helper_pid_alloc_idr_alloc_cyclic(struct pid_namespace *ns,
					   int start, int end);
void rust_helper_pid_alloc_idr_remove(struct pid_namespace *ns, int nr);
void rust_helper_pid_alloc_idr_replace(struct pid_namespace *ns,
				       struct pid *pid, int nr);

void rust_helper_pid_alloc_number_set(struct pid *pid, unsigned int index,
				      int nr, struct pid_namespace *ns);
struct upid *rust_helper_pid_alloc_number(struct pid *pid,
					  unsigned int index);
unsigned int rust_helper_pid_alloc_allocated(struct pid_namespace *ns);
bool rust_helper_pid_alloc_disabled(struct pid_namespace *ns);
void rust_helper_pid_alloc_count_inc(struct pid_namespace *ns);
void rust_helper_pid_alloc_active_get(struct pid_namespace *ns);
int rust_helper_pid_alloc_pidfs_add(struct pid *pid);

int rust_pid_alloc(struct pid_namespace *ns, pid_t *arg_set_tid,
		   size_t arg_set_tid_size, struct pid **out_pid);

#ifdef CONFIG_PID_ALLOCATION_KUNIT_TEST
int rust_pid_alloc_test_pid_min(unsigned int cursor);
int rust_pid_alloc_test_map_idr_error(int error, unsigned int cyclic);
#endif

#endif /* CONFIG_RUST_PID_ALLOCATION */

#endif /* _LINUX_PID_ALLOCATION_RUST_H */
