/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_UNMAP_RUST_H
#define _LINUX_VMA_UNMAP_RUST_H

#include <linux/list.h>
#include <linux/mm.h>

#ifdef CONFIG_RUST_VMA_UNMAP

struct rust_vma_unmap_state {
	struct vma_iterator vmi;
	struct list_head userfaultfd;
};

int rust_vma_unmap(struct mm_struct *mm, unsigned long start, size_t len,
		   bool unlock);
int rust_do_vmi_munmap(struct vma_iterator *vmi, struct mm_struct *mm,
		       unsigned long start, size_t len, struct list_head *uf,
		       bool unlock);

void rust_helper_vma_unmap_state_init(struct rust_vma_unmap_state *state,
				      struct mm_struct *mm,
				      unsigned long start);
int rust_helper_vma_unmap_write_lock_killable(struct mm_struct *mm);
void rust_helper_vma_unmap_write_unlock(struct mm_struct *mm);
unsigned long rust_helper_vma_unmap_task_size(void);
struct vm_area_struct *
rust_helper_vma_unmap_find(struct vma_iterator *vmi, unsigned long end);
int rust_helper_vma_unmap_align(struct vma_iterator *vmi,
				struct vm_area_struct *vma,
				struct mm_struct *mm,
				unsigned long start, unsigned long end,
				struct list_head *uf, bool unlock);
void rust_helper_vma_unmap_userfault_complete(struct mm_struct *mm,
					      struct list_head *uf);

#ifdef CONFIG_VMA_UNMAP_KUNIT_TEST

int rust_vma_unmap_test_range(unsigned long start, size_t len,
			      unsigned long task_size, unsigned long *end);

#endif /* CONFIG_VMA_UNMAP_KUNIT_TEST */

#endif /* CONFIG_RUST_VMA_UNMAP */

#endif /* _LINUX_VMA_UNMAP_RUST_H */
