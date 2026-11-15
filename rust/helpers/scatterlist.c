// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-direction.h>

#ifdef CONFIG_RUST_KFIFO
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

__rust_helper void rust_helper_kfifo_set_sg(struct scatterlist *sg,
					    const void *buf, unsigned int len,
					    dma_addr_t dma_address, bool set_dma)
{
	sg_set_buf(sg, buf, len);

	if (set_dma) {
		sg_dma_address(sg) = dma_address;
		sg_dma_len(sg) = len;
	}
}
#endif /* CONFIG_RUST_KFIFO */

__rust_helper dma_addr_t rust_helper_sg_dma_address(struct scatterlist *sg)
{
	return sg_dma_address(sg);
}

__rust_helper unsigned int rust_helper_sg_dma_len(struct scatterlist *sg)
{
	return sg_dma_len(sg);
}

__rust_helper struct scatterlist *rust_helper_sg_next(struct scatterlist *sg)
{
	return sg_next(sg);
}

__rust_helper void rust_helper_dma_unmap_sgtable(struct device *dev,
						 struct sg_table *sgt,
						 enum dma_data_direction dir,
						 unsigned long attrs)
{
	return dma_unmap_sgtable(dev, sgt, dir, attrs);
}
