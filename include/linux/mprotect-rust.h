/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MPROTECT_RUST_H
#define _LINUX_MPROTECT_RUST_H

#include <linux/mm.h>
#include <linux/mman.h>

struct rust_mprotect_state;

unsigned long rust_mprotect_do(struct rust_mprotect_state *state);
int rust_mprotect_fixup(struct rust_mprotect_state *state,
			struct vm_area_struct *vma,
			unsigned long start, unsigned long end);

#ifdef CONFIG_RUST_MPROTECT

enum rust_mprotect_constant {
	RUST_MPROTECT_PROT_READ = PROT_READ,
	RUST_MPROTECT_PROT_WRITE = PROT_WRITE,
	RUST_MPROTECT_PROT_EXEC = PROT_EXEC,
	RUST_MPROTECT_PROT_GROWSDOWN = PROT_GROWSDOWN,
	RUST_MPROTECT_PROT_GROWSUP = PROT_GROWSUP,
	RUST_MPROTECT_MM_CP_TRY_CHANGE_WRITABLE = MM_CP_TRY_CHANGE_WRITABLE,
};

unsigned long
rust_helper_mprotect_state_start(struct rust_mprotect_state *state);
size_t rust_helper_mprotect_state_len(struct rust_mprotect_state *state);
unsigned long
rust_helper_mprotect_state_prot(struct rust_mprotect_state *state);
unsigned long rust_helper_mprotect_untagged_addr(unsigned long addr);
int rust_helper_mprotect_write_lock_killable(struct rust_mprotect_state *state);
void rust_helper_mprotect_write_unlock(struct rust_mprotect_state *state);
bool rust_helper_mprotect_read_implies_exec(void);
bool rust_helper_mprotect_validate_prot(unsigned long prot,
					unsigned long start);
bool rust_helper_mprotect_pkey_allowed(struct rust_mprotect_state *state);
void rust_helper_mprotect_iterator_init(struct rust_mprotect_state *state,
					unsigned long start);
struct vm_area_struct *
rust_helper_mprotect_find(struct rust_mprotect_state *state, unsigned long end);
unsigned long rust_helper_mprotect_vma_start(struct vm_area_struct *vma);
unsigned long rust_helper_mprotect_vma_end(struct vm_area_struct *vma);
bool rust_helper_mprotect_vma_growsdown(struct vm_area_struct *vma);
bool rust_helper_mprotect_vma_growsup(struct vm_area_struct *vma);
bool rust_helper_mprotect_vma_mayexec(struct vm_area_struct *vma);
struct vm_area_struct *
rust_helper_mprotect_vma_prev(struct rust_mprotect_state *state);
void rust_helper_mprotect_tlb_gather(struct rust_mprotect_state *state);
void rust_helper_mprotect_tlb_finish(struct rust_mprotect_state *state);
void rust_helper_mprotect_prepare_flags(struct rust_mprotect_state *state,
					struct vm_area_struct *vma,
					unsigned long prot);
bool rust_helper_mprotect_access_flags_invalid(struct rust_mprotect_state *state);
bool rust_helper_mprotect_deny_write_exec(struct rust_mprotect_state *state,
					  struct vm_area_struct *vma);
bool rust_helper_mprotect_validate_flags(struct rust_mprotect_state *state);
int rust_helper_mprotect_security_file(struct vm_area_struct *vma,
				       unsigned long reqprot,
				       unsigned long prot);
bool rust_helper_mprotect_has_vm_op(struct vm_area_struct *vma);
int rust_helper_mprotect_call_vm_op(struct rust_mprotect_state *state,
				    struct vm_area_struct *vma,
				    unsigned long start, unsigned long end);
unsigned long rust_helper_mprotect_iter_end(struct rust_mprotect_state *state);

void rust_helper_mprotect_load_old_flags(struct rust_mprotect_state *state,
					 struct vm_area_struct *vma);
bool rust_helper_mprotect_vma_sealed(struct vm_area_struct *vma);
bool rust_helper_mprotect_same_flags(struct rust_mprotect_state *state);
bool rust_helper_mprotect_pfn_check_required(struct rust_mprotect_state *state);
int rust_helper_mprotect_pfn_check(struct rust_mprotect_state *state,
				   struct vm_area_struct *vma,
				   unsigned long start, unsigned long end);
bool rust_helper_mprotect_new_writable(struct rust_mprotect_state *state);
bool rust_helper_mprotect_may_expand(struct rust_mprotect_state *state,
				     bool use_new_flags,
				     unsigned long nrpages);
bool rust_helper_mprotect_old_charge_exempt(struct rust_mprotect_state *state);
int rust_helper_mprotect_enough_memory(struct rust_mprotect_state *state,
				       unsigned long nrpages);
void rust_helper_mprotect_set_new_account(struct rust_mprotect_state *state);
bool rust_helper_mprotect_old_accounted(struct rust_mprotect_state *state);
bool rust_helper_mprotect_new_accounted(struct rust_mprotect_state *state);
bool rust_helper_mprotect_can_clear_account(struct vm_area_struct *vma);
void rust_helper_mprotect_clear_new_account(struct rust_mprotect_state *state);
struct vm_area_struct *
rust_helper_mprotect_modify(struct rust_mprotect_state *state,
			    struct vm_area_struct *vma,
			    unsigned long start, unsigned long end);
bool rust_helper_mprotect_is_error(struct vm_area_struct *vma);
int rust_helper_mprotect_pointer_error(struct vm_area_struct *vma);
void rust_helper_mprotect_set_prev(struct rust_mprotect_state *state,
				   struct vm_area_struct *vma);
void rust_helper_mprotect_start_write(struct vm_area_struct *vma);
void rust_helper_mprotect_reset_flags(struct rust_mprotect_state *state,
				      struct vm_area_struct *vma);
bool rust_helper_mprotect_manual_write_upgrade(struct vm_area_struct *vma);
void rust_helper_mprotect_set_page_prot(struct vm_area_struct *vma);
void rust_helper_mprotect_change_protection(struct rust_mprotect_state *state,
					    struct vm_area_struct *vma,
					    unsigned long start,
					    unsigned long end,
					    unsigned int cp_flags);
void rust_helper_mprotect_unaccount(unsigned long nrpages);
bool rust_helper_mprotect_old_locked(struct rust_mprotect_state *state);
bool rust_helper_mprotect_old_write(struct rust_mprotect_state *state);
bool rust_helper_mprotect_old_shared(struct rust_mprotect_state *state);
void rust_helper_mprotect_populate(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end);
void rust_helper_mprotect_account_old(struct rust_mprotect_state *state,
				      long nrpages);
void rust_helper_mprotect_account_new(struct rust_mprotect_state *state,
				      long nrpages);
void rust_helper_mprotect_perf_event(struct vm_area_struct *vma);

#endif /* CONFIG_RUST_MPROTECT */

#endif /* _LINUX_MPROTECT_RUST_H */
