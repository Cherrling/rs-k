/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BRK_RUST_H
#define _LINUX_BRK_RUST_H

#include <linux/list.h>
#include <linux/mm.h>

struct rust_brk_state;

unsigned long rust_brk(struct rust_brk_state *state);

#ifdef CONFIG_RUST_BRK

struct rust_brk_state {
	struct mm_struct *mm;
	unsigned long brk;
	struct vma_iterator vmi;
	struct list_head uf;
};

int rust_helper_brk_write_lock_killable(struct rust_brk_state *state);
void rust_helper_brk_write_unlock(struct rust_brk_state *state);
unsigned long rust_helper_brk_current(struct rust_brk_state *state);
unsigned long rust_helper_brk_start(struct rust_brk_state *state);
bool rust_helper_brk_use_end_data(void);
unsigned long rust_helper_brk_end_data(struct rust_brk_state *state);
int rust_helper_brk_check_data_rlimit(struct rust_brk_state *state);
void rust_helper_brk_iterator_init(struct rust_brk_state *state,
				   unsigned long addr);
struct vm_area_struct *
rust_helper_brk_find(struct rust_brk_state *state, unsigned long end);
unsigned long rust_helper_brk_vma_start(struct vm_area_struct *vma);
unsigned long rust_helper_brk_stack_guard_gap(void);
unsigned long rust_helper_brk_vma_start_gap(struct vm_area_struct *vma);
struct vm_area_struct *rust_helper_brk_vma_prev(struct rust_brk_state *state);
int rust_helper_brk_check_limits(unsigned long addr, unsigned long len);
int rust_helper_brk_expand(struct rust_brk_state *state,
			   struct vm_area_struct *vma,
			   unsigned long start, unsigned long len);
int rust_helper_brk_align_unmap(struct rust_brk_state *state,
				struct vm_area_struct *vma,
				unsigned long start, unsigned long end);
bool rust_helper_brk_should_populate(struct rust_brk_state *state);
void rust_helper_brk_set_address(struct rust_brk_state *state,
				 unsigned long value);
void rust_helper_brk_userfault_complete(struct rust_brk_state *state);
void rust_helper_brk_populate(unsigned long start, unsigned long len);

#endif /* CONFIG_RUST_BRK */

#endif /* _LINUX_BRK_RUST_H */
