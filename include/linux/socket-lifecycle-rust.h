/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust socket and sock lifecycle.
 */

#ifndef _LINUX_SOCKET_LIFECYCLE_RUST_H
#define _LINUX_SOCKET_LIFECYCLE_RUST_H

#include <linux/net.h>
#include <linux/types.h>

struct file;
struct inode;
struct module;
struct net;
struct net_proto_family;
struct proto;
struct sock;
struct socket;
struct vfsmount;

#ifdef CONFIG_RUST_SOCKET_LIFECYCLE

struct sock *sk_alloc(struct net *net, int family, gfp_t priority,
		      struct proto *prot, int kern);
void sk_free(struct sock *sk);
void sk_destruct(struct sock *sk);

int rust_sock_close(struct inode *inode, struct file *file);
void rust_socket_sk_free_final(struct sock *sk);

/*
 * Socket inode and file binding primitives.
 */
struct inode *rust_helper_socket_new_pseudo_inode(void);
struct socket *rust_helper_socket_from_inode(struct inode *inode);
struct inode *rust_helper_socket_inode_from_socket(struct socket *sock);
void rust_helper_socket_init_inode(struct inode *inode);
const struct file_operations *rust_helper_socket_file_operations(void);
struct vfsmount *rust_helper_socket_mount(void);
const char *rust_helper_socket_protocol_name(struct socket *sock);
void rust_helper_socket_file_set_nowait(struct file *file);
void rust_helper_socket_bind_file(struct socket *sock, struct file *file);
void rust_helper_socket_stream_open(struct socket *sock, struct file *file);
void rust_helper_socket_file_set_fsnotify_mode(struct file *file);

/*
 * Socket close and protocol dispatch primitives.
 */
const struct proto_ops *rust_helper_socket_ops(struct socket *sock);
void rust_helper_socket_inode_lock(struct inode *inode);
void rust_helper_socket_inode_unlock(struct inode *inode);
bool rust_helper_socket_fasync_pending(struct socket *sock);
void rust_helper_socket_warn_fasync(void);
void rust_helper_socket_clear_file(struct socket *sock);

/*
 * Socket family registration and creation primitives.
 */
void rust_helper_socket_rcu_read_lock(void);
void rust_helper_socket_rcu_read_unlock(void);
const struct net_proto_family *
rust_helper_socket_family_lookup(unsigned int family);
void rust_helper_socket_request_family(unsigned int family);
struct net *rust_helper_socket_current_net(void);
int rust_helper_socket_update_protocol(int family, int type, int protocol);
int rust_helper_socket_security_create(int family, int type, int protocol,
				       int kern);
int rust_helper_socket_security_post_create(struct socket *sock, int family,
					    int type, int protocol, int kern);
void rust_helper_socket_warn_obsolete_packet(void);
void rust_helper_socket_warn_no_sockets(void);
void rust_helper_socket_warn_create_sk(struct socket *sock,
				       const struct net_proto_family *family_ops,
				       int family, int type, int protocol);
long rust_helper_socket_ptr_err(const void *value);

/*
 * Sock allocation and release primitives.
 */
struct sock *rust_helper_sock_prot_alloc(struct proto *prot, gfp_t priority);
void rust_helper_sock_prot_free(struct proto *prot, struct sock *sk);
struct proto *rust_helper_sock_prot_creator(struct sock *sk);
int rust_helper_sock_security_alloc(struct sock *sk, int family,
				    gfp_t priority);
void rust_helper_sock_security_free(struct sock *sk);
bool rust_helper_sock_module_get(struct proto *prot);
void rust_helper_sock_module_put(struct proto *prot);
void rust_helper_sock_cgroup_free(struct sock *sk);
void rust_helper_sock_memcg_free(struct sock *sk);
void rust_helper_sock_owner_put(struct sock *sk);

void rust_helper_sock_set_family(struct sock *sk, int family);
void rust_helper_sock_set_prot(struct sock *sk, struct proto *prot);
void rust_helper_sock_set_bypass(struct sock *sk, bool bypass);
void rust_helper_sock_set_kern(struct sock *sk, bool kern);
bool rust_helper_sock_bypass(struct net *net);
void rust_helper_sock_lock_init(struct sock *sk);
void rust_helper_sock_set_net_refcnt(struct sock *sk, bool refcnt);
bool rust_helper_sock_net_refcnt(struct sock *sk);
void rust_helper_sock_net_get_tracked(struct sock *sk, struct net *net,
				      gfp_t priority);
void rust_helper_sock_net_passive_inc(struct net *net);
void rust_helper_sock_net_passive_dec(struct net *net);
void rust_helper_sock_net_tracker_alloc(struct sock *sk, struct net *net,
					bool refcnted, gfp_t priority);
void rust_helper_sock_net_tracker_free(struct sock *sk, struct net *net,
				       bool refcnted);
void rust_helper_sock_net_set(struct sock *sk, struct net *net);
void rust_helper_sock_inuse_add(struct net *net, int value);
void rust_helper_sock_wmem_init(struct sock *sk);
bool rust_helper_sock_wmem_put(struct sock *sk);
void rust_helper_sock_memcg_alloc(struct sock *sk);
void rust_helper_sock_cgroup_alloc(struct sock *sk);
void rust_helper_sock_update_classid(struct sock *sk);
void rust_helper_sock_update_netprioidx(struct sock *sk);
void rust_helper_sock_tx_queue_clear(struct sock *sk);
bool rust_helper_sock_ref_put(struct sock *sk);

bool rust_helper_sock_rcu_free(struct sock *sk);
bool rust_helper_sock_has_reuseport(struct sock *sk);
void rust_helper_sock_reuseport_detach(struct sock *sk);
void rust_helper_sock_call_rcu(struct sock *sk,
			       void (*callback)(struct callback_head *head));
struct sock *rust_helper_sock_from_rcu(struct callback_head *head);
void rust_helper_sock_call_user_destruct(struct sock *sk);
void *rust_helper_sock_filter(struct sock *sk);
bool rust_helper_sock_filter_uncharge(struct sock *sk, void *filter);
void rust_helper_sock_disable_timestamp(struct sock *sk);
void rust_helper_sock_bpf_storage_free(struct sock *sk);
bool rust_helper_sock_omem_leak(struct sock *sk);
void rust_helper_sock_put_frag(struct sock *sk);
void rust_helper_sock_put_peer_resources(struct sock *sk);
void rust_helper_sock_put_net_tracked(struct sock *sk, struct net *net);
struct net *rust_helper_sock_net(struct sock *sk);
bool rust_helper_sock_diag_has_listeners(struct sock *sk);
void rust_helper_sock_diag_broadcast_destroy(struct sock *sk);

/*
 * Test injection and observation. These helpers are no-ops outside the
 * focused KUnit configuration.
 */
#define RUST_SOCKET_TEST_FAIL_INODE		1
#define RUST_SOCKET_TEST_FAIL_SECURITY_CREATE	2
#define RUST_SOCKET_TEST_FAIL_FAMILY		3
#define RUST_SOCKET_TEST_FAIL_FAMILY_MODULE	4
#define RUST_SOCKET_TEST_FAIL_CREATE		5
#define RUST_SOCKET_TEST_FAIL_OPS_MODULE	6
#define RUST_SOCKET_TEST_FAIL_SECURITY_POST	7
#define RUST_SOCKET_TEST_FAIL_SK_ALLOC		8
#define RUST_SOCKET_TEST_FAIL_SK_SECURITY	9
#define RUST_SOCKET_TEST_FAIL_SK_MODULE		10

#define RUST_SOCKET_TEST_COUNTER_INODE		0
#define RUST_SOCKET_TEST_COUNTER_RELEASE	1
#define RUST_SOCKET_TEST_COUNTER_SK_ALLOC	2
#define RUST_SOCKET_TEST_COUNTER_SK_FREE		3
#define RUST_SOCKET_TEST_COUNTER_RCU_FREE	4
#define RUST_SOCKET_TEST_COUNTER_COUNT		5

#ifdef CONFIG_SOCKET_LIFECYCLE_KUNIT_TEST
bool rust_helper_socket_test_fail(unsigned int stage);
void rust_helper_socket_test_record(unsigned int counter);
unsigned int rust_helper_socket_test_counter(unsigned int counter);
void rust_socket_test_reset(void);
void rust_socket_test_fail_next(unsigned int stage);
void rust_socket_test_wmem_put_amount(struct sock *sk, unsigned int amount);
void rust_socket_test_set_rcu_free(struct sock *sk);
void rust_socket_test_synchronize_rcu(void);
int rust_socket_test_wmem_count(struct sock *sk);
#endif

#endif /* CONFIG_RUST_SOCKET_LIFECYCLE */

#endif /* _LINUX_SOCKET_LIFECYCLE_RUST_H */
