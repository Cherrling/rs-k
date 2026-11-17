/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface for the native Rust IDR implementation.
 */

#ifndef _LINUX_IDR_RUST_H
#define _LINUX_IDR_RUST_H

#include <linux/idr.h>

#ifdef CONFIG_RUST_IDR_ALLOC

enum rust_idr_constant {
	RUST_IDR_ENOMEM = -ENOMEM,
	RUST_IDR_ENOSPC = -ENOSPC,
	RUST_IDR_EINVAL = -EINVAL,
	RUST_IDR_FREE_MARK = IDR_FREE,
};

void rust_idr_prepare(struct idr *idr);
void rust_idr_commit(struct idr *idr, struct xa_state *xas, void *ptr);
void rust_idr_warn_invalid_start(void);

int rust_idr_alloc_u32(struct idr *idr, void *ptr, u32 *nextid,
		       unsigned long max, gfp_t gfp);
int rust_idr_alloc(struct idr *idr, void *ptr, int start, int end,
		   gfp_t gfp);
int rust_idr_alloc_cyclic(struct idr *idr, void *ptr, int start, int end,
			  gfp_t gfp);

#endif /* CONFIG_RUST_IDR_ALLOC */

#endif /* _LINUX_IDR_RUST_H */
