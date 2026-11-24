/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface for the native Rust IDR implementation.
 */

#ifndef _LINUX_IDR_RUST_H
#define _LINUX_IDR_RUST_H

#include <linux/idr.h>

#if defined(CONFIG_RUST_IDR_ALLOC) || defined(CONFIG_RUST_IDR_NEXT)
enum rust_idr_constant {
	RUST_IDR_ENOMEM = -ENOMEM,
	RUST_IDR_ENOSPC = -ENOSPC,
	RUST_IDR_EINVAL = -EINVAL,
	RUST_IDR_FREE_MARK = IDR_FREE,
};
#endif

#ifdef CONFIG_RUST_IDR_ALLOC
void rust_idr_prepare(struct idr *idr);
void rust_idr_commit(struct idr *idr, struct xa_state *xas, void *ptr);
void rust_idr_warn_invalid_start(void);
#endif /* CONFIG_RUST_IDR_ALLOC */

#ifdef CONFIG_RUST_IDR_NEXT
void rust_idr_warn_id_overflow(void);
#endif /* CONFIG_RUST_IDR_NEXT */

#endif /* _LINUX_IDR_RUST_H */
