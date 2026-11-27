/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared path lookup state and helpers for the native Rust implementation.
 */

#ifndef _LINUX_PATH_LOOKUP_RUST_H
#define _LINUX_PATH_LOOKUP_RUST_H

#include <linux/audit.h>
#include <linux/delayed_call.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/types.h>

/*
 * Type of the last component on LOOKUP_PARENT.
 */
enum last_type {
	LAST_NORM,
	LAST_ROOT,
	LAST_DOT,
	LAST_DOTDOT,
};

#define EMBEDDED_LEVELS 2

struct nameidata {
	struct path path;
	struct qstr last;
	struct path root;
	struct inode *inode; /* path.dentry.d_inode */
	unsigned int flags, state;
	unsigned seq, next_seq, m_seq, r_seq;
	enum last_type last_type;
	unsigned depth;
	int total_link_count;
	struct saved {
		struct path link;
		struct delayed_call done;
		const char *name;
		unsigned seq;
	} *stack, internal[EMBEDDED_LEVELS];
	struct filename *name;
	const char *pathname;
	struct nameidata *saved;
	unsigned root_seq;
	int dfd;
	vfsuid_t dir_vfsuid;
	umode_t dir_mode;
} __randomize_layout;

#define ND_ROOT_PRESET 1
#define ND_ROOT_GRABBED 2
#define ND_JUMPED 4

#ifdef CONFIG_RUST_PATH_LOOKUP
enum rust_path_lookup_flags {
	RUST_PATH_LOOKUP_DOWN = LOOKUP_DOWN,
	RUST_PATH_LOOKUP_MOUNTPOINT = LOOKUP_MOUNTPOINT,
	RUST_PATH_LOOKUP_REVAL = LOOKUP_REVAL,
	RUST_PATH_LOOKUP_RCU = LOOKUP_RCU,
};

enum rust_path_audit_flags {
	RUST_PATH_AUDIT_NOEVAL = AUDIT_INODE_NOEVAL,
	RUST_PATH_AUDIT_PARENT = AUDIT_INODE_PARENT,
};

int filename_lookup(int dfd, struct filename *name, unsigned flags,
		    struct path *path, const struct path *root);
int path_lookupat(struct nameidata *nd, unsigned flags, struct path *path);
int filename_parentat(int dfd, struct filename *name, unsigned int flags,
		      struct path *parent, struct qstr *last,
		      enum last_type *type);

void rust_helper_path_set_nameidata(struct nameidata *nd, int dfd,
				    struct filename *name,
				    const struct path *root);
void rust_helper_path_restore_nameidata(void);
void rust_helper_path_init_root(struct path *path, struct dentry *dentry,
				struct vfsmount *mnt);
const char *rust_helper_path_init(struct nameidata *nd, unsigned int flags);
int rust_helper_path_link_path_walk(const char *name, struct nameidata *nd);
const char *rust_helper_path_lookup_last(struct nameidata *nd);
int rust_helper_path_handle_lookup_down(struct nameidata *nd);
bool rust_helper_path_has_mountpoint(struct nameidata *nd);
void rust_helper_path_clear_jumped(struct nameidata *nd);
int rust_helper_path_complete_walk(struct nameidata *nd);
bool rust_helper_path_requires_directory(struct nameidata *nd);
bool rust_helper_path_can_lookup(struct nameidata *nd);
void rust_helper_path_take_path(struct nameidata *nd, struct path *path);
void rust_helper_path_terminate_walk(struct nameidata *nd);
void rust_helper_path_get_last(struct nameidata *nd, struct qstr *last);
enum last_type rust_helper_path_last_type(struct nameidata *nd);
void rust_helper_path_audit_inode(struct filename *name,
				  const struct dentry *dentry,
				  unsigned int audit_flags);
void rust_helper_path_put(const struct path *path);
struct filename *rust_helper_path_getname_kernel(const char *name);
void rust_helper_path_putname(struct filename *name);
bool rust_helper_path_is_err(const void *value);
void *rust_helper_path_err_ptr(long error);
long rust_helper_path_ptr_err(const void *value);

#ifdef CONFIG_PATH_LOOKUP_KUNIT_TEST
#define RUST_PATH_TEST_FAIL_ECHILD 1
#define RUST_PATH_TEST_FAIL_ESTALE 2

void rust_path_test_reset(void);
void rust_path_test_fail_lookup(unsigned int error);
void rust_path_test_fail_parent(unsigned int error);
unsigned int rust_path_test_lookup_attempts(void);
unsigned int rust_path_test_lookup_flags(void);
unsigned int rust_path_test_parent_attempts(void);
unsigned int rust_path_test_parent_flags(void);
#endif
#endif /* CONFIG_RUST_PATH_LOOKUP */

#endif /* _LINUX_PATH_LOOKUP_RUST_H */
