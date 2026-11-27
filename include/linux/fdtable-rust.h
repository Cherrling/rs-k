/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FDTABLE_RUST_H
#define _LINUX_FDTABLE_RUST_H

#include <linux/types.h>

struct fdtable;
struct fd;
struct file;
struct files_struct;
struct task_struct;

struct file *file_close_fd_locked(struct files_struct *files, unsigned int fd);

#ifdef CONFIG_RUST_FDTABLE_LIFECYCLE

struct files_struct *rust_helper_fdtable_current_files(void);
struct files_struct *rust_helper_fdtable_task_files(struct task_struct *task);
struct files_struct *
rust_helper_fdtable_detach_task_files(struct task_struct *task);
void rust_helper_fdtable_task_lock(struct task_struct *task);
void rust_helper_fdtable_task_unlock(struct task_struct *task);

struct files_struct *rust_helper_fdtable_alloc_files(void);
void rust_helper_fdtable_init_files(struct files_struct *files);
void rust_helper_fdtable_free_files(struct files_struct *files);

struct fdtable *rust_helper_fdtable_alloc(unsigned int nr);
void rust_helper_fdtable_free(struct fdtable *fdt);
void rust_helper_fdtable_free_rcu(struct fdtable *fdt);
bool rust_helper_fdtable_is_embedded(struct files_struct *files,
				     struct fdtable *fdt);

void rust_helper_fdtable_lock(struct files_struct *files);
void rust_helper_fdtable_unlock(struct files_struct *files);
bool rust_helper_fdtable_resizing(const struct files_struct *files);
void rust_helper_fdtable_set_resizing(struct files_struct *files, bool value);
void rust_helper_fdtable_wait_resize(struct files_struct *files);
void rust_helper_fdtable_wake_resize(struct files_struct *files);

int rust_helper_fdtable_count(const struct files_struct *files);
int rust_helper_fdtable_count_acquire(const struct files_struct *files);
bool rust_helper_fdtable_put_count(struct files_struct *files);
unsigned int rust_helper_fdtable_next_fd(const struct files_struct *files);
void rust_helper_fdtable_set_next_fd(struct files_struct *files,
				     unsigned int fd);

struct fdtable *rust_helper_fdtable_current(struct files_struct *files);
struct fdtable *rust_helper_fdtable_raw(struct files_struct *files);
unsigned int rust_helper_fdtable_max_fds(const struct fdtable *fdt);
void rust_helper_fdtable_publish(struct files_struct *files,
				 struct fdtable *fdt);
void rust_helper_fdtable_synchronize_rcu(void);
void rust_helper_fdtable_read_lock(void);
void rust_helper_fdtable_read_unlock(void);
void rust_helper_fdtable_read_lock_sched(void);
void rust_helper_fdtable_read_unlock_sched(void);

struct file *rust_helper_fdtable_get_fd_raw(const struct fdtable *fdt,
					    unsigned int fd);
struct file *rust_helper_fdtable_entry_raw(struct file **entry);
struct file **rust_helper_fdtable_fdentry(struct fdtable *fdt,
					  unsigned int fd);
void rust_helper_fdtable_set_fd(struct fdtable *fdt, unsigned int fd,
				struct file *file);
void rust_helper_fdtable_clear_fd(struct fdtable *fdt, unsigned int fd);

unsigned long rust_helper_fdtable_open_word(const struct fdtable *fdt,
					    unsigned int word);
unsigned long rust_helper_fdtable_get_bit(const unsigned long *bitmap,
					  unsigned int bit);
unsigned int rust_helper_fdtable_find_next_zero(const unsigned long *bitmap,
						unsigned int size,
						unsigned int offset);
unsigned int rust_helper_fdtable_find_last_bit(const unsigned long *bitmap,
					       unsigned int size);
void rust_helper_fdtable_set_bit(unsigned long *bitmap, unsigned int bit);
void rust_helper_fdtable_clear_bit(unsigned long *bitmap, unsigned int bit);
void rust_helper_fdtable_copy(struct fdtable *new_fdt,
			      struct fdtable *old_fdt);
void rust_helper_fdtable_copy_bitmaps(struct fdtable *new_fdt,
				      struct fdtable *old_fdt,
				      unsigned int copy_words);
void rust_helper_fdtable_clear_entries(struct fdtable *fdt,
				       unsigned int start,
				       unsigned int end);

unsigned long rust_helper_fdtable_nofile(void);
unsigned int rust_helper_fdtable_nr_open(void);
int rust_helper_fdtable_close_file(struct file *file,
				   struct files_struct *files);
void rust_helper_fdtable_resched(void);
void rust_helper_fdtable_assert_locked(struct files_struct *files);

bool rust_helper_fdtable_file_backing(const struct file *file);
u32 rust_helper_fdtable_file_mode(const struct file *file);
bool rust_helper_fdtable_file_ref_get(struct file *file);
void rust_helper_fdtable_get_file(struct file *file);
u32 rust_helper_fdtable_fmode_path(void);
bool rust_helper_fdtable_warn_backing(void);

void rust_fdtable_put_unused_locked(struct files_struct *files, unsigned int fd);
int rust_fdtable_expand_files(struct files_struct *files, unsigned int nr);
int rust_fdtable_alloc(unsigned int start, unsigned int end,
		       unsigned int flags);
struct file *rust_fdtable_fget_rcu(struct files_struct *files,
				   unsigned int fd, u32 mask);

#ifdef CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST
#define RUST_FDTABLE_TEST_FAIL_ALLOC	1
#define RUST_FDTABLE_TEST_FAIL_FILES	2

u32 rust_fdtable_test_alloc_count(void);
u32 rust_fdtable_test_copy_count(void);
u32 rust_fdtable_test_close_count(void);
u32 rust_fdtable_test_failure_count(void);
void rust_fdtable_test_fail_next(u32 stage);
struct file *rust_fdtable_test_fget(struct files_struct *files,
				    unsigned int fd, u32 mask);
int rust_fdtable_test_alloc_range(unsigned int start, unsigned int end,
				  unsigned int flags);
void rust_fdtable_test_install(struct files_struct *files, unsigned int fd,
			       struct file *file);
struct fd rust_fdtable_test_fdget(struct files_struct *files,
				  unsigned int fd, u32 mask);
#endif /* CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST */

#endif /* CONFIG_RUST_FDTABLE_LIFECYCLE */

#endif /* _LINUX_FDTABLE_RUST_H */
