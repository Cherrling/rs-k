// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the generic kernel FIFO implementation.
 *
 * Copyright (C) 2024 Diego Vieira <diego.daniel.professional@gmail.com>
 */
#include <kunit/test.h>

#include <linux/dma-mapping.h>
#include <linux/kfifo.h>
#include <linux/mman.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>

#define KFIFO_SIZE 32
#define N_ELEMENTS 5

struct kfifo_test_priv {
	u8 __user *user;
};

static void kfifo_test_require_user_mapping(struct kunit *test)
{
	struct kfifo_test_priv *priv = test->priv;

	if (!priv->user)
		kunit_skip(test, "Userspace testing requires an MMU");
}

static void kfifo_test_reset_should_clear_the_fifo(struct kunit *test)
{
	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	kfifo_put(&my_fifo, 1);
	kfifo_put(&my_fifo, 2);
	kfifo_put(&my_fifo, 3);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 3);

	kfifo_reset(&my_fifo);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 0);
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static void kfifo_test_define_should_define_an_empty_fifo(struct kunit *test)
{
	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	KUNIT_EXPECT_TRUE(test, kfifo_initialized(&my_fifo));
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 0);
}

static void kfifo_test_len_should_ret_n_of_stored_elements(struct kunit *test)
{
	u8 buffer1[N_ELEMENTS];

	for (int i = 0; i < N_ELEMENTS; i++)
		buffer1[i] = i + 1;

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 0);

	kfifo_in(&my_fifo, buffer1, N_ELEMENTS);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), N_ELEMENTS);

	kfifo_in(&my_fifo, buffer1, N_ELEMENTS);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), N_ELEMENTS * 2);

	kfifo_reset(&my_fifo);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 0);
}

static void kfifo_test_put_should_insert_and_get_should_pop(struct kunit *test)
{
	u8 out_data = 0;
	int processed_elements;
	u8 elements[] = { 3, 5, 11 };

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	// If the fifo is empty, get returns 0
	processed_elements = kfifo_get(&my_fifo, &out_data);
	KUNIT_EXPECT_EQ(test, processed_elements, 0);
	KUNIT_EXPECT_EQ(test, out_data, 0);

	for (int i = 0; i < 3; i++)
		kfifo_put(&my_fifo, elements[i]);

	for (int i = 0; i < 3; i++) {
		processed_elements = kfifo_get(&my_fifo, &out_data);
		KUNIT_EXPECT_EQ(test, processed_elements, 1);
		KUNIT_EXPECT_EQ(test, out_data, elements[i]);
	}
}

static void kfifo_test_in_should_insert_multiple_elements(struct kunit *test)
{
	u8 in_buffer[] = { 11, 25, 65 };
	u8 out_data;
	int processed_elements;

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	kfifo_in(&my_fifo, in_buffer, 3);

	for (int i = 0; i < 3; i++) {
		processed_elements = kfifo_get(&my_fifo, &out_data);
		KUNIT_EXPECT_EQ(test, processed_elements, 1);
		KUNIT_EXPECT_EQ(test, out_data, in_buffer[i]);
	}
}

static void kfifo_test_out_should_pop_multiple_elements(struct kunit *test)
{
	u8 in_buffer[] = { 11, 25, 65 };
	u8 out_buffer[3];
	int copied_elements;

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	for (int i = 0; i < 3; i++)
		kfifo_put(&my_fifo, in_buffer[i]);

	copied_elements = kfifo_out(&my_fifo, out_buffer, 3);
	KUNIT_EXPECT_EQ(test, copied_elements, 3);

	for (int i = 0; i < 3; i++)
		KUNIT_EXPECT_EQ(test, out_buffer[i], in_buffer[i]);
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static void kfifo_test_dec_init_should_define_an_empty_fifo(struct kunit *test)
{
	DECLARE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	INIT_KFIFO(my_fifo);

	// my_fifo is a struct with an inplace buffer
	KUNIT_EXPECT_FALSE(test, __is_kfifo_ptr(&my_fifo));

	KUNIT_EXPECT_TRUE(test, kfifo_initialized(&my_fifo));
}

static void kfifo_test_define_should_equal_declare_init(struct kunit *test)
{
	// declare a variable my_fifo of type struct kfifo of u8
	DECLARE_KFIFO(my_fifo1, u8, KFIFO_SIZE);
	// initialize the my_fifo variable
	INIT_KFIFO(my_fifo1);

	// DEFINE_KFIFO declares the variable with the initial value
	// essentially the same as calling DECLARE_KFIFO and INIT_KFIFO
	DEFINE_KFIFO(my_fifo2, u8, KFIFO_SIZE);

	// my_fifo1 and my_fifo2 have the same size
	KUNIT_EXPECT_EQ(test, sizeof(my_fifo1), sizeof(my_fifo2));
	KUNIT_EXPECT_EQ(test, kfifo_initialized(&my_fifo1),
			kfifo_initialized(&my_fifo2));
	KUNIT_EXPECT_EQ(test, kfifo_is_empty(&my_fifo1),
			kfifo_is_empty(&my_fifo2));
}

static void kfifo_test_alloc_should_initiliaze_a_ptr_fifo(struct kunit *test)
{
	int ret;
	DECLARE_KFIFO_PTR(my_fifo, u8);

	INIT_KFIFO(my_fifo);

	// kfifo_initialized returns false signaling the buffer pointer is NULL
	KUNIT_EXPECT_FALSE(test, kfifo_initialized(&my_fifo));

	// kfifo_alloc allocates the buffer
	ret = kfifo_alloc(&my_fifo, KFIFO_SIZE, GFP_KERNEL);
	KUNIT_EXPECT_EQ_MSG(test, ret, 0, "Memory allocation should succeed");
	KUNIT_EXPECT_TRUE(test, kfifo_initialized(&my_fifo));

	// kfifo_free frees the buffer
	kfifo_free(&my_fifo);
}

static void kfifo_test_peek_should_not_remove_elements(struct kunit *test)
{
	u8 out_data;
	int processed_elements;

	DEFINE_KFIFO(my_fifo, u8, KFIFO_SIZE);

	// If the fifo is empty, peek returns 0
	processed_elements = kfifo_peek(&my_fifo, &out_data);
	KUNIT_EXPECT_EQ(test, processed_elements, 0);

	kfifo_put(&my_fifo, 3);
	kfifo_put(&my_fifo, 5);
	kfifo_put(&my_fifo, 11);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 3);

	processed_elements = kfifo_peek(&my_fifo, &out_data);
	KUNIT_EXPECT_EQ(test, processed_elements, 1);
	KUNIT_EXPECT_EQ(test, out_data, 3);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 3);

	// Using peek doesn't remove the element
	// so the read element and the fifo length
	// remains the same
	processed_elements = kfifo_peek(&my_fifo, &out_data);
	KUNIT_EXPECT_EQ(test, processed_elements, 1);
	KUNIT_EXPECT_EQ(test, out_data, 3);

	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 3);
}

static void kfifo_test_wrap_and_linear_should_preserve_order(struct kunit *test)
{
	DEFINE_KFIFO(my_fifo, u8, 8);
	u8 in_buffer[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	u8 wrap_input[7] = { 6, 7, 0, 1, 2, 3, 4 };
	u8 out_buffer[8];
	u8 expected[8] = { 5, 6, 7, 0, 1, 2, 3, 4 };
	u8 *linear;

	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out_buffer, 0), 0);
	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, in_buffer, 6), 6);
	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out_buffer, 5), 5);
	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, wrap_input, 7), 7);

	KUNIT_EXPECT_EQ(test, kfifo_out_linear_ptr(&my_fifo, &linear, 8), 3);
	KUNIT_EXPECT_PTR_EQ(test, linear, &my_fifo.buf[5]);
	KUNIT_EXPECT_MEMEQ(test, linear, &expected[0], 3);

	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out_buffer, 32), 8);
	KUNIT_EXPECT_MEMEQ(test, out_buffer, expected, sizeof(expected));
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static void kfifo_test_element_size_should_scale_byte_accesses(struct kunit *test)
{
	DEFINE_KFIFO(my_fifo, u16, 8);
	u16 in_buffer[8] = { 10, 11, 12, 13, 14, 15, 16, 17 };
	u16 wrap_input[7] = { 16, 17, 10, 11, 12, 13, 14 };
	u16 out_buffer[8];
	u16 expected[8] = { 15, 16, 17, 10, 11, 12, 13, 14 };

	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, in_buffer, 6), 6);
	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out_buffer, 5), 5);
	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, wrap_input, 7), 7);
	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out_buffer, 32), 8);
	KUNIT_EXPECT_MEMEQ(test, out_buffer, expected, sizeof(expected));
}

static void kfifo_test_records_should_frame_and_skip(struct kunit *test)
{
	STRUCT_KFIFO_REC_2(16) my_fifo;
	const u8 first[] = { 1, 2, 3 };
	const u8 second[] = { 4, 5 };
	u8 out[4];

	INIT_KFIFO(my_fifo);

	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, first, sizeof(first)),
			sizeof(first));
	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, second, sizeof(second)),
			sizeof(second));
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo),
			sizeof(first) + sizeof(second) + 4);
	KUNIT_EXPECT_EQ(test, kfifo_peek_len(&my_fifo), sizeof(first));

	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out, 2), 2);
	KUNIT_EXPECT_MEMEQ(test, out, first, 2);
	KUNIT_EXPECT_EQ(test, kfifo_peek_len(&my_fifo), sizeof(second));

	KUNIT_EXPECT_EQ(test, kfifo_peek(&my_fifo, out), sizeof(u8));
	KUNIT_EXPECT_EQ(test, out[0], second[0]);
	KUNIT_EXPECT_EQ(test, kfifo_peek_len(&my_fifo), sizeof(second));

	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out, sizeof(out)),
			sizeof(second));
	KUNIT_EXPECT_MEMEQ(test, out, second, sizeof(second));
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static void kfifo_test_record_wrap_and_bounds(struct kunit *test)
{
	STRUCT_KFIFO_REC_2(16) my_fifo;
	u8 large[13] = {};
	u8 small[] = { 0xaa, 0xbb };
	u8 out[2];

	INIT_KFIFO(my_fifo);

	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, large, 17), 0);
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));

	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, large, sizeof(large)),
			sizeof(large));
	kfifo_skip(&my_fifo);
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));

	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, small, sizeof(small)),
			sizeof(small));
	KUNIT_EXPECT_EQ(test, kfifo_peek_len(&my_fifo), sizeof(small));
	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out, sizeof(out)),
			sizeof(small));
	KUNIT_EXPECT_MEMEQ(test, out, small, sizeof(small));
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static void kfifo_test_user_copy_roundtrip(struct kunit *test)
{
	struct kfifo_test_priv *priv = test->priv;
	DEFINE_KFIFO(my_fifo, u8, 8);
	const u8 expected[] = { 9, 8, 7, 6, 5, 4, 3, 2 };
	u8 actual[N_ELEMENTS] = {};
	unsigned int copied;

	kfifo_test_require_user_mapping(test);
	KUNIT_ASSERT_EQ_MSG(test,
			    copy_to_user(priv->user, expected, sizeof(expected)), 0,
			    "failed to initialize user buffer");

	KUNIT_EXPECT_EQ(test,
			kfifo_from_user(&my_fifo, priv->user, 64, &copied), 0);
	KUNIT_EXPECT_EQ(test, copied, sizeof(expected));
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), sizeof(expected));

	KUNIT_EXPECT_EQ(test,
			kfifo_to_user(&my_fifo, priv->user + 16, N_ELEMENTS,
				      &copied),
			0);
	KUNIT_EXPECT_EQ(test, copied, N_ELEMENTS);
	KUNIT_EXPECT_EQ(test,
			copy_from_user(actual, priv->user + 16, sizeof(actual)),
			0);
	KUNIT_EXPECT_MEMEQ(test, actual, expected, sizeof(actual));
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo),
			sizeof(expected) - N_ELEMENTS);
}

static void kfifo_test_user_copy_should_scale_elements(struct kunit *test)
{
	struct kfifo_test_priv *priv = test->priv;
	DEFINE_KFIFO(my_fifo, u16, 8);
	const u16 expected[] = { 100, 101, 102, 103, 104, 105 };
	u16 actual[4] = {};
	unsigned int copied;

	kfifo_test_require_user_mapping(test);
	KUNIT_ASSERT_EQ_MSG(test,
			    copy_to_user(priv->user, expected, sizeof(expected)), 0,
			    "failed to initialize user buffer");

	KUNIT_EXPECT_EQ(test,
			kfifo_from_user(&my_fifo, priv->user, 12, &copied), 0);
	KUNIT_EXPECT_EQ(test, copied, 12);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 6);

	KUNIT_EXPECT_EQ(test,
			kfifo_to_user(&my_fifo, priv->user + 32, 8, &copied), 0);
	KUNIT_EXPECT_EQ(test, copied, sizeof(actual));
	KUNIT_EXPECT_EQ(test,
			copy_from_user(actual, priv->user + 32, sizeof(actual)),
			0);
	KUNIT_EXPECT_MEMEQ(test, actual, expected, sizeof(actual));
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 2);
}

static void kfifo_test_user_copy_should_reject_bad_range(struct kunit *test)
{
	DEFINE_KFIFO(my_fifo, u8, 8);
	u8 __user *bad = (u8 __user *)~0UL;
	unsigned int copied = 99;

	kfifo_test_require_user_mapping(test);
	KUNIT_EXPECT_EQ(test, kfifo_from_user(&my_fifo, bad, 1, &copied),
			-EFAULT);
	KUNIT_EXPECT_EQ(test, copied, 0);
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));

	KUNIT_EXPECT_EQ(test, kfifo_put(&my_fifo, 0x5a), 1);
	copied = 99;
	KUNIT_EXPECT_EQ(test, kfifo_to_user(&my_fifo, bad, 1, &copied),
			-EFAULT);
	KUNIT_EXPECT_EQ(test, copied, 0);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 1);
}

static void kfifo_test_record_user_copy_roundtrip(struct kunit *test)
{
	struct kfifo_test_priv *priv = test->priv;

	STRUCT_KFIFO_REC_2(16) my_fifo;
	const u8 expected[] = { 21, 22, 23, 24, 25 };
	u8 actual[sizeof(expected)] = {};
	unsigned int copied;

	INIT_KFIFO(my_fifo);
	kfifo_test_require_user_mapping(test);
	KUNIT_ASSERT_EQ_MSG(test,
			    copy_to_user(priv->user, expected, sizeof(expected)), 0,
			    "failed to initialize user buffer");

	KUNIT_EXPECT_EQ(test,
			kfifo_from_user(&my_fifo, priv->user, sizeof(expected),
					&copied),
			0);
	KUNIT_EXPECT_EQ(test, copied, sizeof(expected));
	KUNIT_EXPECT_EQ(test, kfifo_peek_len(&my_fifo), sizeof(expected));

	KUNIT_EXPECT_EQ(test,
			kfifo_to_user(&my_fifo, priv->user + 16, sizeof(actual),
				      &copied),
			0);
	KUNIT_EXPECT_EQ(test, copied, sizeof(actual));
	KUNIT_EXPECT_EQ(test,
			copy_from_user(actual, priv->user + 16, sizeof(actual)),
			0);
	KUNIT_EXPECT_MEMEQ(test, actual, expected, sizeof(actual));
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static void kfifo_test_dma_ranges_should_split_at_wrap(struct kunit *test)
{
	DEFINE_KFIFO(my_fifo, u8, 8);
	struct scatterlist sg[2];
	const u8 in_buffer[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	u8 out_buffer[5];
	unsigned int nents;

	sg_init_table(sg, ARRAY_SIZE(sg));
	KUNIT_EXPECT_EQ(test, kfifo_in(&my_fifo, in_buffer, 6), 6);
	KUNIT_EXPECT_EQ(test, kfifo_out(&my_fifo, out_buffer, 5), 5);

	nents = kfifo_dma_in_prepare_mapped(&my_fifo, sg, ARRAY_SIZE(sg), 4,
					    0x1000);
	KUNIT_EXPECT_EQ(test, nents, 2);
	KUNIT_EXPECT_EQ(test, sg[0].length, 2);
	KUNIT_EXPECT_EQ(test, sg[1].length, 2);
	KUNIT_EXPECT_PTR_EQ(test, sg_virt(&sg[0]), &my_fifo.buf[6]);
	KUNIT_EXPECT_PTR_EQ(test, sg_virt(&sg[1]), &my_fifo.buf[0]);
	KUNIT_EXPECT_EQ(test, sg_dma_address(&sg[0]), 0x1006);
	KUNIT_EXPECT_EQ(test, sg_dma_address(&sg[1]), 0x1000);
	KUNIT_EXPECT_EQ(test, sg_dma_len(&sg[0]), 2);
	KUNIT_EXPECT_EQ(test, sg_dma_len(&sg[1]), 2);

	kfifo_dma_in_finish(&my_fifo, 4);
	KUNIT_EXPECT_EQ(test, kfifo_len(&my_fifo), 5);

	nents = kfifo_dma_out_prepare_mapped(&my_fifo, sg, ARRAY_SIZE(sg), 8,
					     0x2000);
	KUNIT_EXPECT_EQ(test, nents, 2);
	KUNIT_EXPECT_EQ(test, sg[0].length, 3);
	KUNIT_EXPECT_EQ(test, sg[1].length, 2);
	KUNIT_EXPECT_EQ(test, sg_dma_address(&sg[0]), 0x2005);
	KUNIT_EXPECT_EQ(test, sg_dma_address(&sg[1]), 0x2000);
	KUNIT_EXPECT_EQ(test,
			kfifo_dma_out_prepare(&my_fifo, sg, ARRAY_SIZE(sg), 0),
			0);
}

static void kfifo_test_record_dma_should_frame_segments(struct kunit *test)
{
	STRUCT_KFIFO_REC_1(16) my_fifo;
	struct scatterlist sg[2];
	unsigned int nents;

	INIT_KFIFO(my_fifo);
	sg_init_table(sg, ARRAY_SIZE(sg));

	nents = kfifo_dma_in_prepare(&my_fifo, sg, ARRAY_SIZE(sg), 4);
	KUNIT_EXPECT_EQ(test, nents, 1);
	KUNIT_EXPECT_EQ(test, sg[0].length, 4);
	kfifo_dma_in_finish(&my_fifo, 4);
	KUNIT_EXPECT_EQ(test, kfifo_peek_len(&my_fifo), 4);

	nents = kfifo_dma_out_prepare(&my_fifo, sg, ARRAY_SIZE(sg), 4);
	KUNIT_EXPECT_EQ(test, nents, 1);
	KUNIT_EXPECT_EQ(test, sg[0].length, 4);
	kfifo_dma_out_finish(&my_fifo, 4);
	KUNIT_EXPECT_TRUE(test, kfifo_is_empty(&my_fifo));
}

static int kfifo_test_init(struct kunit *test)
{
	struct kfifo_test_priv *priv;
	unsigned long user_addr;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);
	test->priv = priv;

	if (!IS_ENABLED(CONFIG_MMU))
		return 0;

	user_addr = kunit_vm_mmap(test, NULL, 0, PAGE_SIZE,
				  PROT_READ | PROT_WRITE,
				  MAP_ANONYMOUS | MAP_PRIVATE, 0);
	KUNIT_ASSERT_NE_MSG(test, user_addr, 0,
			    "Could not create userspace mapping");
	priv->user = (u8 __user *)user_addr;

	return 0;
}

static struct kunit_case kfifo_test_cases[] = {
	KUNIT_CASE(kfifo_test_reset_should_clear_the_fifo),
	KUNIT_CASE(kfifo_test_define_should_define_an_empty_fifo),
	KUNIT_CASE(kfifo_test_len_should_ret_n_of_stored_elements),
	KUNIT_CASE(kfifo_test_put_should_insert_and_get_should_pop),
	KUNIT_CASE(kfifo_test_in_should_insert_multiple_elements),
	KUNIT_CASE(kfifo_test_out_should_pop_multiple_elements),
	KUNIT_CASE(kfifo_test_dec_init_should_define_an_empty_fifo),
	KUNIT_CASE(kfifo_test_define_should_equal_declare_init),
	KUNIT_CASE(kfifo_test_alloc_should_initiliaze_a_ptr_fifo),
	KUNIT_CASE(kfifo_test_peek_should_not_remove_elements),
	KUNIT_CASE(kfifo_test_wrap_and_linear_should_preserve_order),
	KUNIT_CASE(kfifo_test_element_size_should_scale_byte_accesses),
	KUNIT_CASE(kfifo_test_records_should_frame_and_skip),
	KUNIT_CASE(kfifo_test_record_wrap_and_bounds),
	KUNIT_CASE(kfifo_test_user_copy_roundtrip),
	KUNIT_CASE(kfifo_test_user_copy_should_scale_elements),
	KUNIT_CASE(kfifo_test_user_copy_should_reject_bad_range),
	KUNIT_CASE(kfifo_test_record_user_copy_roundtrip),
	KUNIT_CASE(kfifo_test_dma_ranges_should_split_at_wrap),
	KUNIT_CASE(kfifo_test_record_dma_should_frame_segments),
	{},
};

static struct kunit_suite kfifo_test_module = {
	.name = "kfifo",
	.init = kfifo_test_init,
	.test_cases = kfifo_test_cases,
};

kunit_test_suites(&kfifo_test_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Diego Vieira <diego.daniel.professional@gmail.com>");
MODULE_DESCRIPTION("KUnit test for the kernel FIFO");
