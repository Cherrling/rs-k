// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/bitmap.h>
#include <linux/kthread.h>

#define FIND_BIT_TEST_BITS (BITS_PER_LONG * 4 + 7)
#define FIND_BIT_TEST_WORKERS 4
#define FIND_BIT_TEST_ITERATIONS 5000
#define FIND_BIT_TEST_TIMEOUT_MS 5000

enum find_op {
	FIND_ONE,
	FIND_AND,
	FIND_ANDNOT,
	FIND_AND_AND,
	FIND_NOT,
	FIND_OR,
	FIND_AND_ANDNOT,
};

static bool test_bit_at(const unsigned long *bitmap, unsigned int bit)
{
	return bitmap[bit / BITS_PER_LONG] & BIT(bit % BITS_PER_LONG);
}

static bool find_op_matches(enum find_op op, const unsigned long *bitmap1,
			    const unsigned long *bitmap2,
			    const unsigned long *bitmap3, unsigned int bit)
{
	bool first = test_bit_at(bitmap1, bit);

	switch (op) {
	case FIND_ONE:
		return first;
	case FIND_AND:
		return first && test_bit_at(bitmap2, bit);
	case FIND_ANDNOT:
		return first && !test_bit_at(bitmap2, bit);
	case FIND_AND_AND:
		return first && test_bit_at(bitmap2, bit) &&
		       test_bit_at(bitmap3, bit);
	case FIND_NOT:
		return !first;
	case FIND_OR:
		return first || test_bit_at(bitmap2, bit);
	case FIND_AND_ANDNOT:
		return first && test_bit_at(bitmap2, bit) &&
		       !test_bit_at(bitmap3, bit);
	}

	return false;
}

static unsigned long reference_first(enum find_op op,
				     const unsigned long *bitmap1,
				     const unsigned long *bitmap2,
				     const unsigned long *bitmap3,
				     unsigned int nbits)
{
	unsigned int bit;

	for (bit = 0; bit < nbits; bit++)
		if (find_op_matches(op, bitmap1, bitmap2, bitmap3, bit))
			return bit;

	return nbits;
}

static unsigned long reference_next(enum find_op op,
				    const unsigned long *bitmap1,
				    const unsigned long *bitmap2,
				    const unsigned long *bitmap3,
				    unsigned int nbits, unsigned int start)
{
	unsigned int bit;

	for (bit = start; bit < nbits; bit++)
		if (find_op_matches(op, bitmap1, bitmap2, bitmap3, bit))
			return bit;

	return nbits;
}

static unsigned long reference_nth(enum find_op op,
				   const unsigned long *bitmap1,
				   const unsigned long *bitmap2,
				   const unsigned long *bitmap3,
				   unsigned int nbits, unsigned int n)
{
	unsigned int bit;

	for (bit = 0; bit < nbits; bit++) {
		if (!find_op_matches(op, bitmap1, bitmap2, bitmap3, bit))
			continue;
		if (n-- == 0)
			return bit;
	}

	return nbits;
}

static unsigned long reference_last(const unsigned long *bitmap,
				    unsigned int nbits)
{
	unsigned int bit;

	for (bit = nbits; bit > 0; bit--)
		if (test_bit_at(bitmap, bit - 1))
			return bit - 1;

	return nbits;
}

static unsigned long reference_clump8(unsigned long *clump,
				      const unsigned long *bitmap,
				      unsigned int nbits, unsigned int offset)
{
	unsigned long found;
	unsigned int base;
	unsigned int bit;

	found = reference_next(FIND_ONE, bitmap, NULL, NULL, nbits, offset);
	if (found == nbits)
		return nbits;

	base = found & ~7;
	*clump = 0;
	for (bit = 0; bit < 8 && base + bit < nbits; bit++)
		if (test_bit_at(bitmap, base + bit))
			*clump |= BIT(bit);

	return base;
}

static unsigned long rust_first(enum find_op op,
				const unsigned long *bitmap1,
				const unsigned long *bitmap2,
				const unsigned long *bitmap3,
				unsigned int nbits)
{
	switch (op) {
	case FIND_ONE:
		return _find_first_bit(bitmap1, nbits);
	case FIND_AND:
		return _find_first_and_bit(bitmap1, bitmap2, nbits);
	case FIND_ANDNOT:
		return _find_first_andnot_bit(bitmap1, bitmap2, nbits);
	case FIND_AND_AND:
		return _find_first_and_and_bit(bitmap1, bitmap2, bitmap3,
					       nbits);
	case FIND_NOT:
		return _find_first_zero_bit(bitmap1, nbits);
	case FIND_OR:
		return reference_first(op, bitmap1, bitmap2, bitmap3, nbits);
	case FIND_AND_ANDNOT:
		return reference_first(op, bitmap1, bitmap2, bitmap3, nbits);
	}

	return nbits;
}

static unsigned long rust_next(enum find_op op,
			       const unsigned long *bitmap1,
			       const unsigned long *bitmap2,
			       const unsigned long *bitmap3,
			       unsigned int nbits, unsigned int start)
{
	switch (op) {
	case FIND_ONE:
		return _find_next_bit(bitmap1, nbits, start);
	case FIND_AND:
		return _find_next_and_bit(bitmap1, bitmap2, nbits, start);
	case FIND_ANDNOT:
		return _find_next_andnot_bit(bitmap1, bitmap2, nbits, start);
	case FIND_OR:
		return _find_next_or_bit(bitmap1, bitmap2, nbits, start);
	case FIND_NOT:
		return _find_next_zero_bit(bitmap1, nbits, start);
	case FIND_AND_AND:
		return reference_next(op, bitmap1, bitmap2, bitmap3, nbits,
				      start);
	case FIND_AND_ANDNOT:
		return reference_next(op, bitmap1, bitmap2, bitmap3, nbits,
				      start);
	}

	return nbits;
}

static unsigned long rust_nth(enum find_op op,
			      const unsigned long *bitmap1,
			      const unsigned long *bitmap2,
			      const unsigned long *bitmap3,
			      unsigned int nbits, unsigned int n)
{
	switch (op) {
	case FIND_ONE:
		return __find_nth_bit(bitmap1, nbits, n);
	case FIND_AND:
		return __find_nth_and_bit(bitmap1, bitmap2, nbits, n);
	default:
		return reference_nth(op, bitmap1, bitmap2, bitmap3, nbits, n);
	}
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

static void find_bit_rust_test_first_next(struct kunit *test)
{
	static const unsigned int sizes[] = {
		0, 1, 2, 7,
		BITS_PER_LONG - 1, BITS_PER_LONG, BITS_PER_LONG + 1,
		2 * BITS_PER_LONG - 1, 2 * BITS_PER_LONG,
		2 * BITS_PER_LONG + 1, FIND_BIT_TEST_BITS - 1,
		FIND_BIT_TEST_BITS,
	};
	DECLARE_BITMAP(bitmap1, FIND_BIT_TEST_BITS);
	DECLARE_BITMAP(bitmap2, FIND_BIT_TEST_BITS);
	DECLARE_BITMAP(bitmap3, FIND_BIT_TEST_BITS);
	static const enum find_op first_ops[] = {
		FIND_ONE, FIND_AND, FIND_ANDNOT, FIND_AND_AND, FIND_NOT,
	};
	static const enum find_op next_ops[] = {
		FIND_ONE, FIND_AND, FIND_ANDNOT, FIND_OR, FIND_NOT,
	};
	unsigned int seed_index;
	unsigned int size_index;
	unsigned int op_index;

	for (seed_index = 1; seed_index <= 8; seed_index++) {
		for (size_index = 0; size_index < ARRAY_SIZE(sizes);
		     size_index++) {
			unsigned int nbits = sizes[size_index];
			unsigned int start;

			bitmap_zero(bitmap1, FIND_BIT_TEST_BITS);
			bitmap_zero(bitmap2, FIND_BIT_TEST_BITS);
			bitmap_zero(bitmap3, FIND_BIT_TEST_BITS);
			make_pattern(bitmap1, nbits, seed_index);
			make_pattern(bitmap2, nbits, seed_index + 101);
			make_pattern(bitmap3, nbits, seed_index + 1009);

			for (op_index = 0; op_index < ARRAY_SIZE(first_ops);
			     op_index++) {
				enum find_op op = first_ops[op_index];
				unsigned long actual;
				unsigned long expected;

				actual = rust_first(op, bitmap1, bitmap2,
						    bitmap3, nbits);
				expected = reference_first(op, bitmap1, bitmap2,
							   bitmap3, nbits);
				KUNIT_EXPECT_EQ_MSG(test, actual, expected,
						    "first op=%d nbits=%u seed=%u",
						    op, nbits, seed_index);
			}

			for (op_index = 0; op_index < ARRAY_SIZE(next_ops);
			     op_index++) {
				enum find_op op = next_ops[op_index];

				for (start = 0; start <= nbits + 1; start++) {
					unsigned long actual;
					unsigned long expected;

					actual = rust_next(op, bitmap1, bitmap2,
							   bitmap3, nbits,
							   start);
					expected = reference_next(op, bitmap1,
								  bitmap2,
								  bitmap3,
								  nbits, start);
					KUNIT_EXPECT_EQ_MSG(test, actual,
							    expected,
							    "next op=%d nbits=%u start=%u seed=%u",
							    op, nbits, start,
							    seed_index);
				}
			}
		}
	}
}

static void find_bit_rust_test_nth_last(struct kunit *test)
{
	static const unsigned int sizes[] = {
		0, 1, 2, BITS_PER_LONG - 1, BITS_PER_LONG,
		BITS_PER_LONG + 1, 2 * BITS_PER_LONG - 1,
		2 * BITS_PER_LONG, 2 * BITS_PER_LONG + 1,
		FIND_BIT_TEST_BITS,
	};
	DECLARE_BITMAP(bitmap1, FIND_BIT_TEST_BITS);
	DECLARE_BITMAP(bitmap2, FIND_BIT_TEST_BITS);
	DECLARE_BITMAP(bitmap3, FIND_BIT_TEST_BITS);
	static const enum find_op nth_ops[] = {
		FIND_ONE, FIND_AND,
	};
	unsigned int seed_index;
	unsigned int size_index;
	unsigned int op_index;

	for (seed_index = 1; seed_index <= 8; seed_index++) {
		for (size_index = 0; size_index < ARRAY_SIZE(sizes);
		     size_index++) {
			unsigned int nbits = sizes[size_index];
			unsigned int n;

			bitmap_zero(bitmap1, FIND_BIT_TEST_BITS);
			bitmap_zero(bitmap2, FIND_BIT_TEST_BITS);
			bitmap_zero(bitmap3, FIND_BIT_TEST_BITS);
			make_pattern(bitmap1, nbits, seed_index);
			make_pattern(bitmap2, nbits, seed_index + 101);
			make_pattern(bitmap3, nbits, seed_index + 1009);

			for (op_index = 0; op_index < ARRAY_SIZE(nth_ops);
			     op_index++) {
				enum find_op op = nth_ops[op_index];

				for (n = 0; n <= nbits + 1; n++) {
					unsigned long actual;
					unsigned long expected;

					actual = rust_nth(op, bitmap1, bitmap2,
							  bitmap3, nbits, n);
					expected = reference_nth(op, bitmap1,
								 bitmap2,
								 bitmap3,
								 nbits, n);
					KUNIT_EXPECT_EQ_MSG(test, actual,
							    expected,
							    "nth op=%d nbits=%u n=%u seed=%u",
							    op, nbits, n,
							    seed_index);
				}
			}

			for (n = 0; n <= nbits + 1; n++) {
				unsigned long actual;
				unsigned long expected;

				actual = __find_nth_and_andnot_bit(bitmap1,
								   bitmap2,
								   bitmap3,
								   nbits, n);
				expected = reference_nth(FIND_AND_ANDNOT,
							 bitmap1, bitmap2,
							 bitmap3, nbits, n);
				KUNIT_EXPECT_EQ_MSG(test, actual, expected,
						    "nth-andnot nbits=%u n=%u seed=%u",
						    nbits, n, seed_index);
			}

			KUNIT_EXPECT_EQ(test, _find_last_bit(bitmap1, nbits),
					reference_last(bitmap1, nbits));
		}
	}
}

static void find_bit_rust_test_clump_random(struct kunit *test)
{
	static const unsigned int sizes[] = {
		0, 1, 7, 8, 9, BITS_PER_LONG, BITS_PER_LONG + 7,
		FIND_BIT_TEST_BITS,
	};
	DECLARE_BITMAP(bitmap1, FIND_BIT_TEST_BITS);
	DECLARE_BITMAP(bitmap2, FIND_BIT_TEST_BITS);
	DECLARE_BITMAP(bitmap3, FIND_BIT_TEST_BITS);
	unsigned int seed_index;
	unsigned int size_index;

	for (seed_index = 1; seed_index <= 8; seed_index++) {
		for (size_index = 0; size_index < ARRAY_SIZE(sizes);
		     size_index++) {
			unsigned int nbits = sizes[size_index];
			unsigned int offset;

			bitmap_zero(bitmap1, FIND_BIT_TEST_BITS);
			bitmap_zero(bitmap2, FIND_BIT_TEST_BITS);
			bitmap_zero(bitmap3, FIND_BIT_TEST_BITS);
			make_pattern(bitmap1, nbits, seed_index);
			make_pattern(bitmap2, nbits, seed_index + 101);
			make_pattern(bitmap3, nbits, seed_index + 1009);

			for (offset = 0; offset <= nbits + 1; offset++) {
				unsigned long clump = 0;
				unsigned long expected;
				unsigned long actual;

				expected = reference_clump8(&clump, bitmap1,
							    nbits, offset);

				actual = find_next_clump8(&clump, bitmap1, nbits,
							  offset);
				KUNIT_EXPECT_EQ_MSG(test, actual, expected,
						    "clump nbits=%u offset=%u seed=%u",
						    nbits, offset, seed_index);
			}

			if (nbits) {
				unsigned long random;

				random = find_random_bit(bitmap1, nbits);
				if (random < nbits)
					KUNIT_EXPECT_TRUE(test,
							  test_bit_at(bitmap1,
								      random));
				else
					KUNIT_EXPECT_EQ(test,
							reference_first(FIND_ONE,
									bitmap1,
									bitmap2,
									bitmap3,
									nbits),
							nbits);
			}
		}
	}
}

static void find_bit_rust_test_single_and_empty(struct kunit *test)
{
	DECLARE_BITMAP(bitmap, FIND_BIT_TEST_BITS);
	unsigned int bit;

	bitmap_zero(bitmap, FIND_BIT_TEST_BITS);
	KUNIT_EXPECT_EQ(test, find_random_bit(bitmap, FIND_BIT_TEST_BITS),
			FIND_BIT_TEST_BITS);

	for (bit = 0; bit < FIND_BIT_TEST_BITS; bit++) {
		bitmap_zero(bitmap, FIND_BIT_TEST_BITS);
		__set_bit(bit, bitmap);
		KUNIT_EXPECT_EQ(test,
				find_random_bit(bitmap, FIND_BIT_TEST_BITS), bit);
	}
}

static void find_bit_rust_test_zero_size(struct kunit *test)
{
	unsigned long clump = 0;

	KUNIT_EXPECT_EQ(test, _find_first_bit(NULL, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_first_and_bit(NULL, NULL, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_first_andnot_bit(NULL, NULL, 0), 0);
	KUNIT_EXPECT_EQ(test,
			_find_first_and_and_bit(NULL, NULL, NULL, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_first_zero_bit(NULL, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_next_bit(NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_next_and_bit(NULL, NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_next_andnot_bit(NULL, NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_next_or_bit(NULL, NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_next_zero_bit(NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, __find_nth_bit(NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, __find_nth_and_bit(NULL, NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test,
			__find_nth_and_andnot_bit(NULL, NULL, NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, _find_last_bit(NULL, 0), 0);
	KUNIT_EXPECT_EQ(test, find_next_clump8(&clump, NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, find_random_bit(NULL, 0), 0);
}

static void find_bit_rust_test_inline_adapters(struct kunit *test)
{
	DECLARE_BITMAP(bitmap1, BITS_PER_LONG);
	DECLARE_BITMAP(bitmap2, BITS_PER_LONG);
	unsigned int tail = BITS_PER_LONG / 2;
	unsigned int last = tail - 1;

	bitmap_zero(bitmap1, BITS_PER_LONG);
	bitmap_fill(bitmap2, BITS_PER_LONG);
	__set_bit(1, bitmap1);
	__set_bit(last, bitmap1);
	__clear_bit(1, bitmap2);
	__clear_bit(last, bitmap2);

	KUNIT_EXPECT_EQ(test, find_first_bit(bitmap1, tail), 1);
	KUNIT_EXPECT_EQ(test, find_next_bit(bitmap1, tail, 2), last);
	KUNIT_EXPECT_EQ(test, find_last_bit(bitmap1, tail), last);
	KUNIT_EXPECT_EQ(test, find_nth_bit(bitmap1, tail, 1), last);
	KUNIT_EXPECT_EQ(test, find_first_zero_bit(bitmap2, tail), 1);
	KUNIT_EXPECT_EQ(test, find_next_zero_bit(bitmap2, tail, 2), last);
	KUNIT_EXPECT_EQ(test, find_first_and_bit(bitmap1, bitmap1, tail), 1);
	KUNIT_EXPECT_EQ(test, find_next_and_bit(bitmap1, bitmap1, tail, 2),
			last);
	KUNIT_EXPECT_EQ(test, find_first_andnot_bit(bitmap1, bitmap2, tail), 1);
	KUNIT_EXPECT_EQ(test, find_next_andnot_bit(bitmap1, bitmap2, tail, 2),
			last);
}

struct find_bit_worker {
	const unsigned long *bitmap1;
	const unsigned long *bitmap2;
	const unsigned long *bitmap3;
	unsigned int nbits;
	unsigned long first;
	unsigned long next;
	unsigned long nth;
	unsigned long last;
	atomic_t *ready;
	atomic_t *done;
	atomic_t *start;
	atomic_t *failed;
	atomic64_t *operations;
};

static int find_bit_worker_thread(void *data)
{
	struct find_bit_worker *worker = data;
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < FIND_BIT_TEST_ITERATIONS; i++) {
		unsigned long random;

		if (find_first_bit(worker->bitmap1, worker->nbits) !=
			    worker->first ||
		    find_next_bit(worker->bitmap1, worker->nbits, 11) !=
			    worker->next ||
		    find_nth_bit(worker->bitmap1, worker->nbits, 7) !=
			    worker->nth ||
		    find_last_bit(worker->bitmap1, worker->nbits) !=
			    worker->last) {
			atomic_inc(worker->failed);
			break;
		}

		random = find_random_bit(worker->bitmap1, worker->nbits);
		if (random == worker->nbits || !test_bit_at(worker->bitmap1,
							    random)) {
			atomic_inc(worker->failed);
			break;
		}

		atomic64_inc(worker->operations);
		if (!(i & 63))
			cond_resched();
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();

	return 0;
}

static void find_bit_rust_test_concurrent(struct kunit *test)
{
	struct task_struct *tasks[FIND_BIT_TEST_WORKERS] = {};
	struct find_bit_worker workers[FIND_BIT_TEST_WORKERS];
	DECLARE_BITMAP(bitmap1, FIND_BIT_TEST_BITS);
	DECLARE_BITMAP(bitmap2, FIND_BIT_TEST_BITS);
	DECLARE_BITMAP(bitmap3, FIND_BIT_TEST_BITS);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic64_t operations = ATOMIC64_INIT(0);
	unsigned long deadline;
	unsigned int started = 0;
	unsigned int i;

	bitmap_zero(bitmap1, FIND_BIT_TEST_BITS);
	bitmap_zero(bitmap2, FIND_BIT_TEST_BITS);
	bitmap_zero(bitmap3, FIND_BIT_TEST_BITS);
	make_pattern(bitmap1, FIND_BIT_TEST_BITS, 0x12345678);
	make_pattern(bitmap2, FIND_BIT_TEST_BITS, 0x9abcdef0);
	make_pattern(bitmap3, FIND_BIT_TEST_BITS, 0x0fedcba9);

	for (i = 0; i < FIND_BIT_TEST_WORKERS; i++) {
		workers[i].bitmap1 = bitmap1;
		workers[i].bitmap2 = bitmap2;
		workers[i].bitmap3 = bitmap3;
		workers[i].nbits = FIND_BIT_TEST_BITS;
		workers[i].first = reference_first(FIND_ONE, bitmap1, bitmap2,
						   bitmap3,
						   FIND_BIT_TEST_BITS);
		workers[i].next = reference_next(FIND_ONE, bitmap1, bitmap2,
						 bitmap3, FIND_BIT_TEST_BITS,
						 11);
		workers[i].nth = reference_nth(FIND_ONE, bitmap1, bitmap2,
					       bitmap3, FIND_BIT_TEST_BITS, 7);
		workers[i].last = reference_last(bitmap1, FIND_BIT_TEST_BITS);
		workers[i].ready = &ready;
		workers[i].done = &done;
		workers[i].start = &start;
		workers[i].failed = &failed;
		workers[i].operations = &operations;
		tasks[i] = kthread_run(find_bit_worker_thread, &workers[i],
				       "find-bit-%u", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start find_bit worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(FIND_BIT_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < FIND_BIT_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != FIND_BIT_TEST_WORKERS) {
		KUNIT_FAIL(test, "find_bit workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) < FIND_BIT_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != FIND_BIT_TEST_WORKERS)
		KUNIT_FAIL(test, "find_bit workers did not finish");
cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	if (started == FIND_BIT_TEST_WORKERS &&
	    atomic_read(&done) == FIND_BIT_TEST_WORKERS) {
		KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
		KUNIT_EXPECT_EQ(test, atomic64_read(&operations),
				(s64)FIND_BIT_TEST_WORKERS *
					FIND_BIT_TEST_ITERATIONS);
	}
}

static struct kunit_case find_bit_rust_test_cases[] = {
	KUNIT_CASE(find_bit_rust_test_first_next),
	KUNIT_CASE(find_bit_rust_test_nth_last),
	KUNIT_CASE(find_bit_rust_test_clump_random),
	KUNIT_CASE(find_bit_rust_test_single_and_empty),
	KUNIT_CASE(find_bit_rust_test_zero_size),
	KUNIT_CASE(find_bit_rust_test_inline_adapters),
	KUNIT_CASE(find_bit_rust_test_concurrent),
	{}
};

static struct kunit_suite find_bit_rust_test_suite = {
	.name = "find_bit_rust",
	.test_cases = find_bit_rust_test_cases,
};

kunit_test_suite(find_bit_rust_test_suite);

MODULE_DESCRIPTION("KUnit tests for the native Rust generic bit searches");
MODULE_LICENSE("GPL");
