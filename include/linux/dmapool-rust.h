/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_DMAPOOL_RUST_H
#define _LINUX_DMAPOOL_RUST_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct device;

struct dma_block {
	struct dma_block *next_block;
	dma_addr_t dma;
};

struct dma_pool {
	struct list_head page_list;
	spinlock_t lock; /* protects next_block and allocation counters */
	struct dma_block *next_block;
	size_t nr_blocks;
	size_t nr_active;
	size_t nr_pages;
	struct device *dev;
	unsigned int size;
	unsigned int allocation;
	unsigned int boundary;
	int node;
	char name[32];
	struct list_head pools;
};

struct dma_page {
	struct list_head page_list;
	void *vaddr;
	dma_addr_t dma;
};

#ifdef CONFIG_RUST_DMAPOOL

void rust_dmapool_reg_lock(void);
void rust_dmapool_reg_unlock(void);
void rust_dmapool_pools_lock(void);
void rust_dmapool_pools_unlock(void);
bool rust_dmapool_dev_has_no_pools(struct device *dev);
void rust_dmapool_add_pool(struct dma_pool *pool);
void rust_dmapool_del_pool(struct dma_pool *pool);
int rust_dmapool_create_pools_attr(struct device *dev);
void rust_dmapool_remove_pools_attr(struct device *dev);

struct dma_pool *rust_dmapool_alloc_pool_struct(int node);
void rust_dmapool_free_pool_struct(struct dma_pool *pool);
void rust_dmapool_init_pool(struct dma_pool *pool);
void rust_dmapool_set_pool_name(struct dma_pool *pool, const char *name);

struct dma_page *rust_dmapool_alloc_page(struct dma_pool *pool, gfp_t flags);
void rust_dmapool_free_page(struct dma_pool *pool, struct dma_page *page);
void rust_dmapool_free_page_struct(struct dma_page *page);
void rust_dmapool_add_page(struct dma_pool *pool, struct dma_page *page);
void rust_dmapool_del_page(struct dma_page *page);
struct dma_page *rust_dmapool_first_page(struct dma_pool *pool);
struct dma_page *rust_dmapool_next_page(struct dma_pool *pool,
					struct dma_page *page);

unsigned long rust_dmapool_lock(struct dma_pool *pool);
void rust_dmapool_unlock(struct dma_pool *pool, unsigned long flags);
void rust_dmapool_might_alloc(gfp_t flags);
void rust_dmapool_check_block(struct dma_pool *pool,
			      struct dma_block *block, gfp_t flags);
void rust_dmapool_poison_page(struct dma_pool *pool, struct dma_page *page);
bool rust_dmapool_block_error(struct dma_pool *pool, void *vaddr,
			      dma_addr_t dma);
void rust_dmapool_report_busy(struct dma_pool *pool);
void rust_dmapool_zero_block(struct dma_pool *pool, struct dma_block *block,
			     gfp_t flags);

void *rust_dmapool_devres_alloc(void);
void rust_dmapool_devres_add(struct device *dev, void *res);
void rust_dmapool_devres_free(void *res);
int rust_dmapool_devres_release(struct device *dev, struct dma_pool *pool);

#endif /* CONFIG_RUST_DMAPOOL */

#endif /* _LINUX_DMAPOOL_RUST_H */
