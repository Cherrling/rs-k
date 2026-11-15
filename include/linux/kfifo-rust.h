/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust kfifo implementation.
 */

#ifndef _LINUX_KFIFO_RUST_H
#define _LINUX_KFIFO_RUST_H

#include <linux/kfifo.h>

#ifdef CONFIG_RUST_KFIFO

unsigned int rust_kfifo_in(struct __kfifo *fifo,
			   const void *buf, unsigned int len);
unsigned int rust_kfifo_out_peek(struct __kfifo *fifo,
				 void *buf, unsigned int len);
unsigned int rust_kfifo_out_linear(struct __kfifo *fifo,
				   unsigned int *tail, unsigned int n);
unsigned int rust_kfifo_out(struct __kfifo *fifo,
			    void *buf, unsigned int len);
int rust_kfifo_from_user(struct __kfifo *fifo, const void __user *from,
			 unsigned long len, unsigned int *copied);
int rust_kfifo_to_user(struct __kfifo *fifo, void __user *to,
		       unsigned long len, unsigned int *copied);
unsigned int rust_kfifo_dma_in_prepare(struct __kfifo *fifo,
				       struct scatterlist *sgl, int nents,
				       unsigned int len, dma_addr_t dma);
unsigned int rust_kfifo_dma_out_prepare(struct __kfifo *fifo,
					struct scatterlist *sgl, int nents,
					unsigned int len, dma_addr_t dma);

unsigned int rust_kfifo_max_r(unsigned int len, size_t recsize);
unsigned int rust_kfifo_len_r(struct __kfifo *fifo, size_t recsize);
unsigned int rust_kfifo_in_r(struct __kfifo *fifo, const void *buf,
			     unsigned int len, size_t recsize);
unsigned int rust_kfifo_out_peek_r(struct __kfifo *fifo, void *buf,
				   unsigned int len, size_t recsize);
unsigned int rust_kfifo_out_linear_r(struct __kfifo *fifo,
				     unsigned int *tail, unsigned int n,
				     size_t recsize);
unsigned int rust_kfifo_out_r(struct __kfifo *fifo, void *buf,
			      unsigned int len, size_t recsize);
void rust_kfifo_skip_r(struct __kfifo *fifo, size_t recsize);
int rust_kfifo_from_user_r(struct __kfifo *fifo, const void __user *from,
			   unsigned long len, unsigned int *copied,
			   size_t recsize);
int rust_kfifo_to_user_r(struct __kfifo *fifo, void __user *to,
			 unsigned long len, unsigned int *copied,
			 size_t recsize);
unsigned int rust_kfifo_dma_in_prepare_r(struct __kfifo *fifo,
					 struct scatterlist *sgl, int nents,
					 unsigned int len, size_t recsize,
					 dma_addr_t dma);
void rust_kfifo_dma_in_finish_r(struct __kfifo *fifo, unsigned int len,
				size_t recsize);
unsigned int rust_kfifo_dma_out_prepare_r(struct __kfifo *fifo,
					  struct scatterlist *sgl, int nents,
					  unsigned int len, size_t recsize,
					  dma_addr_t dma);

#endif /* CONFIG_RUST_KFIFO */

#endif /* _LINUX_KFIFO_RUST_H */
