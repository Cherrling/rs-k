/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_REGION_TEARDOWN_RUST_H
#define _LINUX_VMA_REGION_TEARDOWN_RUST_H

#include <linux/types.h>

struct file;
struct mm_struct;
struct mmu_gather;
struct unmap_desc;
struct vm_area_struct;

#ifdef CONFIG_RUST_VMA_REGION_TEARDOWN

void rust_vma_region_remove(struct vm_area_struct *vma);
void rust_vma_region_unmap(struct unmap_desc *unmap,
			   struct mmu_gather *tlb);

void rust_helper_vma_region_might_sleep(void);
bool rust_helper_vma_region_needs_close(struct vm_area_struct *vma);
void rust_helper_vma_region_close(struct vm_area_struct *vma);
struct file *rust_helper_vma_region_file(struct vm_area_struct *vma);
void rust_helper_vma_region_fput(struct file *file);
void rust_helper_vma_region_policy_put(struct vm_area_struct *vma);
void rust_helper_vma_region_free_vma(struct vm_area_struct *vma);
struct mm_struct *
rust_helper_vma_region_unmap_mm(struct unmap_desc *unmap);
void rust_helper_vma_region_tlb_gather(struct mmu_gather *tlb,
				       struct mm_struct *mm);
void rust_helper_vma_region_update_hiwater(struct mm_struct *mm);
void rust_helper_vma_region_unmap_vmas(struct mmu_gather *tlb,
				       struct unmap_desc *unmap);
void rust_helper_vma_region_iterator_reset(struct unmap_desc *unmap);
void rust_helper_vma_region_free_pgtables(struct mmu_gather *tlb,
					  struct unmap_desc *unmap);
void rust_helper_vma_region_tlb_finish(struct mmu_gather *tlb);

#endif /* CONFIG_RUST_VMA_REGION_TEARDOWN */

#endif /* _LINUX_VMA_REGION_TEARDOWN_RUST_H */
