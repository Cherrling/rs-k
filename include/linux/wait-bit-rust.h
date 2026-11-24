/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_WAIT_BIT_RUST_H
#define _LINUX_WAIT_BIT_RUST_H

#include <linux/wait_bit.h>

#ifdef CONFIG_RUST_WAIT_BIT

wait_queue_head_t *rust_wait_bit_table(void);
unsigned int rust_wait_bit_hash(unsigned long value);
bool rust_wait_bit_test(struct wait_bit_key *key);
bool rust_wait_bit_test_acquire(struct wait_bit_key *key);
bool rust_wait_bit_test_and_set(struct wait_bit_key *key);
void *rust_wait_bit_current_task(void);
unsigned long rust_wait_bit_jiffies(void);
void rust_wait_bit_schedule(void);
void rust_wait_bit_io_schedule(void);
long rust_wait_bit_schedule_timeout(unsigned long timeout);
bool rust_wait_bit_signal_pending_state(unsigned int mode);
bool rust_wait_bit_time_after_eq(unsigned long now, unsigned long timeout);

#endif /* CONFIG_RUST_WAIT_BIT */

#endif /* _LINUX_WAIT_BIT_RUST_H */
