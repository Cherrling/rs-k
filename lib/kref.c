// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/kref.h>

#ifdef CONFIG_RUST_KREF

/*
 * Rust defines these functions. This object contains only ordinary export
 * metadata so loadable modules can keep using the public API.
 */
EXPORT_SYMBOL(kref_init);
EXPORT_SYMBOL(kref_read);
EXPORT_SYMBOL(kref_get);
EXPORT_SYMBOL(kref_put);
EXPORT_SYMBOL(kref_put_mutex);
EXPORT_SYMBOL(kref_put_lock);
EXPORT_SYMBOL(kref_get_unless_zero);

#endif
