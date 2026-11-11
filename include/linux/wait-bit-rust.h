/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_WAIT_BIT_RUST_H
#define _LINUX_WAIT_BIT_RUST_H

#include <linux/wait_bit.h>

#ifdef CONFIG_RUST_WAIT_BIT

wait_queue_head_t *rust_wait_bit_waitqueue(unsigned long *word, int bit);
wait_queue_head_t *rust_wait_bit_var_waitqueue(void *var);

int rust_wait_bit_wake_bit_function(struct wait_queue_entry *wq_entry,
				    unsigned int mode, int sync, void *key);
int rust_wait_bit_var_wake_function(struct wait_queue_entry *wq_entry,
				    unsigned int mode, int sync, void *key);
struct wait_bit_key *
rust_wait_bit_var_wake_key(struct wait_queue_entry *wq_entry, void *arg);
void rust_wait_bit_init_var_entry(struct wait_bit_queue_entry *wbq_entry,
				  void *var, int flags);

int rust_wait_bit_wait_on_bit(struct wait_queue_head *wq_head,
			      struct wait_bit_queue_entry *wbq_entry,
			      wait_bit_action_f *action, unsigned int mode);
int rust_wait_bit_wait_on_bit_lock(struct wait_queue_head *wq_head,
				   struct wait_bit_queue_entry *wbq_entry,
				   wait_bit_action_f *action,
				   unsigned int mode);
int rust_wait_bit_out_of_line_wait(unsigned long *word, int bit,
				   wait_bit_action_f *action,
				   unsigned int mode);
int rust_wait_bit_out_of_line_wait_timeout(unsigned long *word, int bit,
					   wait_bit_action_f *action,
					   unsigned int mode,
					   unsigned long timeout);
int rust_wait_bit_out_of_line_wait_lock(unsigned long *word, int bit,
					wait_bit_action_f *action,
					unsigned int mode);

void rust_wait_bit_wake_up_bit(struct wait_queue_head *wq_head,
			       unsigned long *word, int bit);
void rust_wait_bit_wake_up(unsigned long *word, int bit);
void rust_wait_bit_wake_up_var(void *var);

wait_queue_head_t *rust_wait_bit_table(void);
unsigned int rust_wait_bit_hash(unsigned long value);
bool rust_wait_bit_test(struct wait_bit_key *key);
bool rust_wait_bit_test_acquire(struct wait_bit_key *key);
bool rust_wait_bit_test_and_set(struct wait_bit_key *key);
void *rust_wait_bit_current_task(void);
unsigned long rust_wait_bit_jiffies(void);

#endif /* CONFIG_RUST_WAIT_BIT */

#endif /* _LINUX_WAIT_BIT_RUST_H */
