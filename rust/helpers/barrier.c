// SPDX-License-Identifier: GPL-2.0

#include <asm/barrier.h>

__rust_helper void rust_helper_mb(void)
{
	mb();
}

__rust_helper void rust_helper_rmb(void)
{
	rmb();
}

__rust_helper void rust_helper_wmb(void)
{
	wmb();
}

__rust_helper void rust_helper_dma_mb(void)
{
	dma_mb();
}

__rust_helper void rust_helper_dma_rmb(void)
{
	dma_rmb();
}

__rust_helper void rust_helper_dma_wmb(void)
{
	dma_wmb();
}

__rust_helper void rust_helper_smp_mb(void)
{
	smp_mb();
}

__rust_helper void rust_helper_smp_wmb(void)
{
	smp_wmb();
}

__rust_helper void rust_helper_smp_rmb(void)
{
	smp_rmb();
}

__rust_helper void rust_helper_smp_mb__before_atomic(void)
{
	/* Order preceding normal accesses before the following atomic. */
	smp_mb__before_atomic();
}

__rust_helper void rust_helper_smp_mb__after_atomic(void)
{
	/* Order the preceding atomic before following normal accesses. */
	smp_mb__after_atomic();
}
