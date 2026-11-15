// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/bitmap.h>

#define BITMAP_OPS_TEST_BITS 257

static bool test_bit_at(const unsigned long *bitmap, unsigned int bit)
{
	return bitmap[bit / BITS_PER_LONG] & BIT(bit % BITS_PER_LONG);
}

static void make_pattern(unsigned long *bitmap, unsigned int nbits,
			 unsigned long seed)
{
	unsigned int words = BITS_TO_LONGS(nbits);
	unsigned int index;
	unsigned int tail = nbits % BITS_PER_LONG;

	for (index = 0; index < words; index++) {
		seed = seed * 6364136223846793005UL + 1442695040888963407UL;
		bitmap[index] = seed ^ (seed >> 17);
	}

	if (tail)
		bitmap[words - 1] &= BITMAP_LAST_WORD_MASK(tail);
}

static void expect_bitmap(struct kunit *test, const char *name,
			  const unsigned long *actual,
			  const unsigned long *expected, unsigned int nbits)
{
	unsigned int bit;

	for (bit = 0; bit < nbits; bit++)
		KUNIT_EXPECT_EQ_MSG(test, test_bit_at(actual, bit),
				    test_bit_at(expected, bit),
				    "%s bit=%u nbits=%u", name, bit, nbits);
}

static unsigned int reference_weight(const unsigned long *bitmap,
				     unsigned int nbits)
{
	unsigned int bit;
	unsigned int weight = 0;

	for (bit = 0; bit < nbits; bit++)
		weight += test_bit_at(bitmap, bit);

	return weight;
}

static unsigned int reference_nth(const unsigned long *bitmap,
				  unsigned int nbits, unsigned int n)
{
	unsigned int bit;

	for (bit = 0; bit < nbits; bit++) {
		if (!test_bit_at(bitmap, bit))
			continue;
		if (n-- == 0)
			return bit;
	}

	return nbits;
}

static void bitmap_ops_test_logical(struct kunit *test)
{
	static const unsigned int sizes[] = {
		0, 1, 7, BITS_PER_LONG - 1, BITS_PER_LONG,
		BITS_PER_LONG + 1, 2 * BITS_PER_LONG - 1,
		2 * BITS_PER_LONG, BITMAP_OPS_TEST_BITS,
	};
	DECLARE_BITMAP(bitmap1, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(bitmap2, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(bitmap3, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(actual, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(expected, BITMAP_OPS_TEST_BITS);
	unsigned int seed;
	unsigned int size_index;

	for (seed = 1; seed <= 6; seed++) {
		for (size_index = 0; size_index < ARRAY_SIZE(sizes);
		     size_index++) {
			unsigned int nbits = sizes[size_index];
			unsigned int bit;
			unsigned int and_weight = 0;
			unsigned int andnot_weight = 0;
			unsigned int weight;
			bool has_and = false;
			bool has_andnot = false;
			bool intersects = false;
			bool subset = true;

			OPTIMIZER_HIDE_VAR(nbits);
			bitmap_zero(bitmap1, BITMAP_OPS_TEST_BITS);
			bitmap_zero(bitmap2, BITMAP_OPS_TEST_BITS);
			bitmap_zero(bitmap3, BITMAP_OPS_TEST_BITS);
			make_pattern(bitmap1, nbits, seed);
			make_pattern(bitmap2, nbits, seed + 101);
			make_pattern(bitmap3, nbits, seed + 1009);

			for (bit = 0; bit < nbits; bit++) {
				bool first = test_bit_at(bitmap1, bit);
				bool second = test_bit_at(bitmap2, bit);

				has_and |= first && second;
				has_andnot |= first && !second;
				intersects |= first && second;
				subset &= !first || second;
				and_weight += first && second;
				andnot_weight += first && !second;
			}

			KUNIT_EXPECT_TRUE(test,
					  bitmap_equal(bitmap1, bitmap1, nbits));
			if (nbits) {
				bitmap_copy(expected, bitmap1, nbits);
				__change_bit((seed * 17) % nbits, expected);
				KUNIT_EXPECT_FALSE(test,
						   bitmap_equal(bitmap1, expected,
								nbits));
			}

			bitmap_zero(expected, BITMAP_OPS_TEST_BITS);
			for (bit = 0; bit < nbits; bit++)
				__assign_bit(bit, expected,
					     test_bit_at(bitmap1, bit) ||
					     test_bit_at(bitmap2, bit));
			KUNIT_EXPECT_TRUE(test,
					  bitmap_or_equal(bitmap1, bitmap2,
							  expected, nbits));
			KUNIT_EXPECT_EQ(test,
					__bitmap_or_equal(bitmap1, bitmap2,
							  expected, nbits),
					true);

			bitmap_fill(actual, nbits);
			bitmap_complement(actual, bitmap1, nbits);
			bitmap_zero(expected, BITMAP_OPS_TEST_BITS);
			for (bit = 0; bit < nbits; bit++)
				__assign_bit(bit, expected,
					     !test_bit_at(bitmap1, bit));
			expect_bitmap(test, "complement", actual, expected,
				      nbits);

			KUNIT_EXPECT_EQ(test,
					bitmap_and(actual, bitmap1, bitmap2,
						   nbits),
					has_and);
			for (bit = 0; bit < nbits; bit++)
				__assign_bit(bit, expected,
					     test_bit_at(bitmap1, bit) &&
					     test_bit_at(bitmap2, bit));
			expect_bitmap(test, "and", actual, expected, nbits);
			KUNIT_EXPECT_EQ(test,
					bitmap_weight_and(bitmap1, bitmap2,
							  nbits),
					and_weight);
			KUNIT_EXPECT_EQ(test,
					bitmap_intersects(bitmap1, bitmap2,
							  nbits),
					intersects);
			KUNIT_EXPECT_EQ(test,
					bitmap_subset(bitmap1, bitmap2, nbits),
					subset);
			KUNIT_EXPECT_EQ(test, bitmap_weight(bitmap1, nbits),
					reference_weight(bitmap1, nbits));

			bitmap_or(actual, bitmap1, bitmap2, nbits);
			for (bit = 0; bit < nbits; bit++)
				__assign_bit(bit, expected,
					     test_bit_at(bitmap1, bit) ||
					     test_bit_at(bitmap2, bit));
			expect_bitmap(test, "or", actual, expected, nbits);

			bitmap_xor(actual, bitmap1, bitmap2, nbits);
			for (bit = 0; bit < nbits; bit++)
				__assign_bit(bit, expected,
					     test_bit_at(bitmap1, bit) ^
					     test_bit_at(bitmap2, bit));
			expect_bitmap(test, "xor", actual, expected, nbits);
			weight = reference_weight(expected, nbits);
			KUNIT_EXPECT_EQ(test,
					bitmap_weighted_xor(actual, bitmap1,
							    bitmap2, nbits),
					weight);

			bitmap_andnot(actual, bitmap1, bitmap2, nbits);
			for (bit = 0; bit < nbits; bit++)
				__assign_bit(bit, expected,
					     test_bit_at(bitmap1, bit) &&
					     !test_bit_at(bitmap2, bit));
			expect_bitmap(test, "andnot", actual, expected, nbits);
			KUNIT_EXPECT_EQ(test,
					bitmap_weight_andnot(bitmap1, bitmap2,
							     nbits),
					andnot_weight);

			bitmap_weighted_or(actual, bitmap1, bitmap2, nbits);
			for (bit = 0; bit < nbits; bit++)
				__assign_bit(bit, expected,
					     test_bit_at(bitmap1, bit) ||
					     test_bit_at(bitmap2, bit));
			expect_bitmap(test, "weighted-or", actual, expected,
				      nbits);

			bitmap_replace(actual, bitmap1, bitmap2, bitmap3, nbits);
			for (bit = 0; bit < nbits; bit++)
				__assign_bit(bit, expected,
					     test_bit_at(bitmap3, bit) ?
					     test_bit_at(bitmap2, bit) :
					     test_bit_at(bitmap1, bit));
			expect_bitmap(test, "replace", actual, expected, nbits);

			for (bit = 0; bit <= nbits + 1; bit++) {
				unsigned int shift;

				bitmap_shift_right(actual, bitmap1, bit, nbits);
				bitmap_zero(expected, BITMAP_OPS_TEST_BITS);
				for (shift = bit; shift < nbits; shift++)
					__assign_bit(shift - bit, expected,
						     test_bit_at(bitmap1,
								 shift));
				expect_bitmap(test, "shift-right", actual,
					      expected, nbits);

				bitmap_shift_left(actual, bitmap1, bit, nbits);
				bitmap_zero(expected, BITMAP_OPS_TEST_BITS);
				for (shift = 0; shift < nbits; shift++)
					if (shift + bit < nbits)
						__assign_bit(shift + bit,
							     expected,
							     test_bit_at(bitmap1,
									 shift));
				expect_bitmap(test, "shift-left", actual,
					      expected, nbits);
			}
		}
	}
}

static void bitmap_ops_test_mapping(struct kunit *test)
{
	DECLARE_BITMAP(src, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(old, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(new, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(actual, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(expected, BITMAP_OPS_TEST_BITS);
	unsigned int nbits = BITMAP_OPS_TEST_BITS;
	unsigned int new_weight;
	unsigned int bit;

	make_pattern(src, nbits, 0x12345678);
	make_pattern(old, nbits, 0x9abcdef0);
	make_pattern(new, nbits, 0x0fedcba9);
	new_weight = reference_weight(new, nbits);

	bitmap_zero(expected, nbits);
	for (bit = 0; bit < nbits; bit++) {
		unsigned int ordinal;
		unsigned int target;

		if (!test_bit_at(src, bit))
			continue;

		if (!test_bit_at(old, bit) || !new_weight) {
			__set_bit(bit, expected);
			continue;
		}

		ordinal = reference_weight(old, bit);
		target = reference_nth(new, nbits, ordinal % new_weight);
		__set_bit(target, expected);
	}

	bitmap_remap(actual, src, old, new, nbits);
	expect_bitmap(test, "remap", actual, expected, nbits);

	for (bit = 0; bit < nbits; bit++) {
		unsigned int target;

		if (!test_bit_at(old, bit) || !new_weight) {
			target = bit;
		} else {
			unsigned int ordinal = reference_weight(old, bit);

			target = reference_nth(new, nbits,
					       ordinal % new_weight);
		}
		KUNIT_EXPECT_EQ_MSG(test, bitmap_bitremap(bit, old, new, nbits),
				    target, "bitmap_bitremap bit=%u", bit);
	}

	bitmap_zero(expected, nbits);
	for (bit = 0; bit < nbits; bit++) {
		unsigned int ordinal;

		if (!test_bit_at(old, bit))
			continue;
		ordinal = reference_weight(old, bit);
		if (test_bit_at(src, ordinal))
			__set_bit(bit, expected);
	}
	bitmap_onto(actual, src, old, nbits);
	expect_bitmap(test, "onto", actual, expected, nbits);

	bitmap_zero(expected, nbits);
	for (bit = 0; bit < nbits; bit++)
		if (test_bit_at(src, bit))
			__set_bit(bit % 13, expected);
	bitmap_fold(actual, src, 13, nbits);
	expect_bitmap(test, "fold", actual, expected, nbits);
}

static void bitmap_ops_test_arrays(struct kunit *test)
{
	static const unsigned int sizes[] = {
		0, 1, 31, 32, 33, 63, 64, 65, 127, 128,
		BITMAP_OPS_TEST_BITS,
	};
	DECLARE_BITMAP(bitmap, BITMAP_OPS_TEST_BITS);
	DECLARE_BITMAP(actual, BITMAP_OPS_TEST_BITS);
	u32 arr32[DIV_ROUND_UP(BITMAP_OPS_TEST_BITS, 32)];
	u64 arr64[DIV_ROUND_UP(BITMAP_OPS_TEST_BITS, 64)];
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(sizes); index++) {
		unsigned int nbits = sizes[index];

		bitmap_zero(bitmap, BITMAP_OPS_TEST_BITS);
		bitmap_zero(actual, BITMAP_OPS_TEST_BITS);
		make_pattern(bitmap, nbits, index + 7);

		memset(arr32, 0xa5, sizeof(arr32));
		bitmap_to_arr32(arr32, bitmap, nbits);
		bitmap_from_arr32(actual, arr32, nbits);
		expect_bitmap(test, "arr32", actual, bitmap, nbits);

		memset(arr64, 0x5a, sizeof(arr64));
		bitmap_to_arr64(arr64, bitmap, nbits);
		bitmap_from_arr64(actual, arr64, nbits);
		expect_bitmap(test, "arr64", actual, bitmap, nbits);
	}
}

static struct kunit_case bitmap_ops_test_cases[] = {
	KUNIT_CASE(bitmap_ops_test_logical),
	KUNIT_CASE(bitmap_ops_test_mapping),
	KUNIT_CASE(bitmap_ops_test_arrays),
	{}
};

static struct kunit_suite bitmap_ops_test_suite = {
	.name = "bitmap_ops",
	.test_cases = bitmap_ops_test_cases,
};

kunit_test_suite(bitmap_ops_test_suite);

MODULE_DESCRIPTION("KUnit tests for the native Rust generic bitmap operations");
MODULE_LICENSE("GPL");
