/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_MUNMAP_GATHER_RUST_H
#define _LINUX_VMA_MUNMAP_GATHER_RUST_H

#include <linux/list.h>
#include <linux/maple_tree.h>
#include <linux/mm.h>

struct rust_vma_munmap_gather_state {
	struct vma_iterator *vmi;
	struct vm_area_struct *first;
	struct vm_area_struct *prev;
	struct vm_area_struct *next;
	struct list_head *uf;
	struct ma_state *mas_detach;
	unsigned long start;
	unsigned long end;
	unsigned long unmap_start;
	unsigned long unmap_end;
	int vma_count;
	bool clear_ptes;
	unsigned long nr_pages;
	unsigned long locked_vm;
	unsigned long nr_accounted;
	unsigned long exec_vm;
	unsigned long stack_vm;
	unsigned long data_vm;
};

enum rust_vma_munmap_account_kind {
	RUST_VMA_MUNMAP_ACCOUNT_NONE,
	RUST_VMA_MUNMAP_ACCOUNT_EXEC,
	RUST_VMA_MUNMAP_ACCOUNT_STACK,
	RUST_VMA_MUNMAP_ACCOUNT_DATA,
};

enum rust_vma_munmap_vma_flags {
	RUST_VMA_MUNMAP_VMA_LOCKED = 1,
	RUST_VMA_MUNMAP_VMA_ACCOUNTED = 2,
};

#ifdef CONFIG_RUST_VMA_MUNMAP_GATHER

int rust_vma_munmap_gather(struct rust_vma_munmap_gather_state *state);

unsigned long rust_helper_vma_munmap_vma_start(struct vm_area_struct *vma);
unsigned long rust_helper_vma_munmap_vma_end(struct vm_area_struct *vma);
int rust_helper_vma_munmap_map_count(struct vm_area_struct *vma);
int rust_helper_vma_munmap_max_map_count(void);
bool rust_helper_vma_munmap_vma_is_sealed(struct vm_area_struct *vma);
int rust_helper_vma_munmap_split(struct vma_iterator *vmi,
				 struct vm_area_struct *vma,
				 unsigned long address, bool new_below);
struct vm_area_struct *
rust_helper_vma_munmap_prev(struct vma_iterator *vmi);
struct vm_area_struct *
rust_helper_vma_munmap_find(struct vma_iterator *vmi, unsigned long end);
void rust_helper_vma_munmap_start_write(struct vm_area_struct *vma);
void rust_helper_vma_munmap_mas_set(struct ma_state *mas_detach, int index);
int rust_helper_vma_munmap_mas_store(struct ma_state *mas_detach,
				     struct vm_area_struct *vma);
void rust_helper_vma_munmap_mark_detached(struct vm_area_struct *vma);
unsigned long rust_helper_vma_munmap_vma_pages(struct vm_area_struct *vma);
unsigned int rust_helper_vma_munmap_vma_flags(struct vm_area_struct *vma);
enum rust_vma_munmap_account_kind
rust_helper_vma_munmap_account_kind(struct vm_area_struct *vma);
int rust_helper_vma_munmap_userfault_prep(struct vm_area_struct *vma,
					  unsigned long start,
					  unsigned long end,
					  struct list_head *uf);
struct vm_area_struct *
rust_helper_vma_munmap_next(struct vma_iterator *vmi);
void rust_helper_vma_munmap_debug_check_vma(struct vm_area_struct *vma,
					    unsigned long start,
					    unsigned long end);
void rust_helper_vma_munmap_debug_gather(struct rust_vma_munmap_gather_state *state);
unsigned long rust_helper_vma_munmap_iter_addr(struct vma_iterator *vmi);
void rust_helper_vma_munmap_iter_prev_range(struct vma_iterator *vmi);

#endif /* CONFIG_RUST_VMA_MUNMAP_GATHER */

#endif /* _LINUX_VMA_MUNMAP_GATHER_RUST_H */
