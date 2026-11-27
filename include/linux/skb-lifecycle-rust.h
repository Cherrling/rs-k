/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust skb lifecycle.
 */

#ifndef _LINUX_SKB_LIFECYCLE_RUST_H
#define _LINUX_SKB_LIFECYCLE_RUST_H

#include <linux/skbuff.h>
#include <linux/types.h>

struct kmem_cache;
struct sock;

#ifdef CONFIG_RUST_SKB_LIFECYCLE

/*
 * Allocation primitives.
 */
bool rust_helper_skb_memalloc_socks(void);
gfp_t rust_helper_skb_gfp_memalloc(gfp_t gfp_mask);
gfp_t rust_helper_skb_gfp_for_cache(gfp_t gfp_mask);
struct kmem_cache *rust_helper_skb_cache(bool fclone);
bool rust_helper_skb_node_requires_fallback(int node);
bool rust_helper_skb_can_use_napi_cache(void);
struct sk_buff *rust_helper_skb_napi_cache_get(bool alloc);
struct sk_buff *rust_helper_skb_napi_cache_get_bh(void);
struct sk_buff *rust_helper_skb_cache_alloc(struct kmem_cache *cache,
					    gfp_t gfp_mask, int node);
void rust_helper_skb_cache_free(struct kmem_cache *cache,
				struct sk_buff *skb);
void rust_helper_skb_clear(struct sk_buff *skb);
void *rust_helper_skb_alloc_head(unsigned int *size, gfp_t gfp_mask,
				 int node, struct sk_buff *skb);
void rust_helper_skb_finalize_around(struct sk_buff *skb, void *data,
				     unsigned int size);
void rust_helper_skb_init_fclone(struct sk_buff *skb);

/*
 * Release primitives.
 */
void rust_helper_skb_release_data(struct sk_buff *skb, u32 reason);
void rust_helper_skb_release_all(struct sk_buff *skb, u32 reason);
void rust_helper_skb_release_head_state(struct sk_buff *skb);
void rust_helper_skb_free_shell(struct sk_buff *skb);
void rust_helper_skb_free_shell_bulk(void **skbs, unsigned int count);
bool rust_helper_skb_unref(struct sk_buff *skb);
struct sk_buff *rust_helper_skb_next(struct sk_buff *skb);
void rust_helper_skb_poison_list(struct sk_buff *skb);
bool rust_helper_skb_reason_is_consumed(u32 reason);
void rust_helper_skb_check_drop_reason(u32 reason);
void rust_helper_skb_trace_consume(struct sk_buff *skb);
void rust_helper_skb_trace_drop(const struct sock *sk, struct sk_buff *skb,
				u32 reason);
bool rust_helper_skb_defer_disabled(void);
bool rust_helper_skb_alloc_cpu_matches(struct sk_buff *skb);
bool rust_helper_skb_shared(struct sk_buff *skb);
void rust_helper_skb_check_napi_context(void);
void rust_helper_skb_dev_consume_any(struct sk_buff *skb);
void rust_helper_skb_attempt_defer_free(struct sk_buff *skb);
void rust_helper_skb_napi_cache_put(struct sk_buff *skb);

/*
 * Clone primitives.
 */
int rust_helper_skb_orphan_frags(struct sk_buff *skb, gfp_t gfp_mask);
bool rust_helper_skb_pfmemalloc(struct sk_buff *skb);
bool rust_helper_skb_fclone_ready(struct sk_buff *skb);
struct sk_buff *rust_helper_skb_fclone_take(struct sk_buff *skb);
struct sk_buff *rust_helper_skb_clone_cache_alloc(gfp_t gfp_mask);
void rust_helper_skb_set_fclone_unavailable(struct sk_buff *skb);
void rust_helper_skb_clone_reset_links(struct sk_buff *skb);
void rust_helper_skb_copy_header(struct sk_buff *new,
				 const struct sk_buff *old);
void rust_helper_skb_clone_copy_fields(struct sk_buff *new,
				       const struct sk_buff *old);
void rust_helper_skb_clone_share_data(struct sk_buff *new,
				      struct sk_buff *old);
void rust_helper_skb_clear_destructor(struct sk_buff *skb);
void rust_helper_skb_set_message_frag_list(struct sk_buff *skb,
					   struct sk_buff *first);
u32 rust_helper_skb_fclone_state(const struct sk_buff *skb);

/*
 * Test injection and observation. These helpers are no-ops outside the
 * focused KUnit configuration.
 */
#define RUST_SKB_TEST_FAIL_SHELL	1
#define RUST_SKB_TEST_FAIL_HEAD		2
#define RUST_SKB_TEST_FAIL_CLONE	3

#define RUST_SKB_TEST_COUNTER_ALLOC	0
#define RUST_SKB_TEST_COUNTER_FREE	1
#define RUST_SKB_TEST_COUNTER_CLONE	2
#define RUST_SKB_TEST_COUNTER_COUNT	3

#ifdef CONFIG_SKB_LIFECYCLE_KUNIT_TEST
bool rust_helper_skb_test_fail(unsigned int stage);
void rust_helper_skb_test_record(unsigned int counter);
unsigned int rust_helper_skb_test_counter(unsigned int counter);
void rust_skb_test_reset(void);
void rust_skb_test_fail_next(unsigned int stage);
#endif

#endif /* CONFIG_RUST_SKB_LIFECYCLE */

#endif /* _LINUX_SKB_LIFECYCLE_RUST_H */
