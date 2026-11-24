// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/rcuwait.h>

#ifdef CONFIG_RUST_RCUWAIT

/*
 * Rust defines these functions. This object contains only ordinary export
 * metadata so loadable modules can keep using the public API.
 */
EXPORT_SYMBOL(rcuwait_init);
EXPORT_SYMBOL(rcuwait_active);
EXPORT_SYMBOL(prepare_to_rcuwait);

#endif
