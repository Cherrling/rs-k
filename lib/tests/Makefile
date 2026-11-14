# SPDX-License-Identifier: GPL-2.0
#
# Makefile for tests of kernel library functions.

# KUnit tests
CFLAGS_bitfield_kunit.o := $(DISABLE_STRUCTLEAK_PLUGIN)
obj-$(CONFIG_BASE64_KUNIT) += base64_kunit.o
obj-$(CONFIG_BITOPS_KUNIT) += bitops_kunit.o
obj-$(CONFIG_BITFIELD_KUNIT) += bitfield_kunit.o
obj-$(CONFIG_BITS_TEST) += test_bits.o
obj-$(CONFIG_SHDI3_KUNIT_TEST) += shdi3_kunit.o
obj-$(CONFIG_BLACKHOLE_DEV_KUNIT_TEST) += blackhole_dev_kunit.o
obj-$(CONFIG_CHECKSUM_KUNIT) += checksum_kunit.o
obj-$(CONFIG_CMDLINE_KUNIT_TEST) += cmdline_kunit.o
obj-$(CONFIG_COMPLETION_KUNIT_TEST) += test_completion.o
obj-$(CONFIG_MUTEX_KUNIT_TEST) += test_mutex.o
obj-$(CONFIG_RWSEM_KUNIT_TEST) += test_rwsem.o
obj-$(CONFIG_RWLOCK_KUNIT_TEST) += test_rwlock.o
obj-$(CONFIG_SEMAPHORE_KUNIT_TEST) += test_semaphore.o
obj-$(CONFIG_WAITQUEUE_KUNIT_TEST) += test_waitqueue.o
obj-$(CONFIG_WAIT_BIT_KUNIT_TEST) += test_wait_bit.o
obj-$(CONFIG_CPUPRI_KUNIT_TEST) += test_cpupri.o
obj-$(CONFIG_TIMERQUEUE_KUNIT_TEST) += test_timerqueue.o
obj-$(CONFIG_ITIMER_KUNIT_TEST) += test_itimer.o
obj-$(CONFIG_HRTIMER_KUNIT_TEST) += test_hrtimer_core.o
obj-$(CONFIG_SCHED_WAKE_KUNIT_TEST) += test_sched_wake.o
obj-$(CONFIG_MM_LIFETIME_KUNIT_TEST) += test_mm_lifetime.o
obj-$(CONFIG_TASK_TEARDOWN_KUNIT_TEST) += test_task_teardown.o
obj-$(CONFIG_TASK_RELEASE_KUNIT_TEST) += test_task_release.o
obj-$(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST) += test_task_group_exit_sync.o
obj-$(CONFIG_SIGNAL_RETARGET_KUNIT_TEST) += test_signal_retarget.o
obj-$(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST) += test_signal_wake_select.o
obj-$(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST) += test_signal_wake_state.o
obj-$(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST) += test_task_exit_notify.o
obj-$(CONFIG_PID_LIFECYCLE_KUNIT_TEST) += test_pid_lifecycle.o
obj-$(CONFIG_PID_ALLOCATION_KUNIT_TEST) += test_pid_allocation.o
obj-$(CONFIG_PID_ASSOCIATION_KUNIT_TEST) += test_pid_association.o
obj-$(CONFIG_PID_NUMBER_KUNIT_TEST) += test_pid_number.o
obj-$(CONFIG_CPUMASK_KUNIT_TEST) += cpumask_kunit.o
obj-$(CONFIG_FFS_KUNIT_TEST) += ffs_kunit.o
CFLAGS_fortify_kunit.o += $(call cc-disable-warning, unsequenced)
CFLAGS_fortify_kunit.o += $(call cc-disable-warning, stringop-overread)
CFLAGS_fortify_kunit.o += $(call cc-disable-warning, stringop-truncation)
CFLAGS_fortify_kunit.o += $(DISABLE_STRUCTLEAK_PLUGIN)
obj-$(CONFIG_FORTIFY_KUNIT_TEST) += fortify_kunit.o
CFLAGS_test_fprobe.o += $(CC_FLAGS_FTRACE)
obj-$(CONFIG_FPROBE_SANITY_TEST) += test_fprobe.o
obj-$(CONFIG_GLOB_KUNIT_TEST) += glob_kunit.o
obj-$(CONFIG_HASHTABLE_KUNIT_TEST) += hashtable_test.o
obj-$(CONFIG_HASH_KUNIT_TEST) += test_hash.o
obj-$(CONFIG_TEST_IOV_ITER) += kunit_iov_iter.o
obj-$(CONFIG_IS_SIGNED_TYPE_KUNIT_TEST) += is_signed_type_kunit.o
obj-$(CONFIG_KPROBES_SANITY_TEST) += test_kprobes.o
obj-$(CONFIG_LIST_KUNIT_TEST) += list-test.o
obj-$(CONFIG_LIST_PRIVATE_KUNIT_TEST) += list-private-test.o
obj-$(CONFIG_KFIFO_KUNIT_TEST) += kfifo_kunit.o
obj-$(CONFIG_TEST_LIST_SORT) += test_list_sort.o
obj-$(CONFIG_LINEAR_RANGES_TEST) += test_linear_ranges.o
obj-$(CONFIG_LIVEUPDATE_TEST) += liveupdate.o

CFLAGS_longest_symbol_kunit.o += $(call cc-disable-warning, missing-prototypes)
obj-$(CONFIG_LONGEST_SYM_KUNIT_TEST) += longest_symbol_kunit.o

obj-$(CONFIG_MEMCPY_KUNIT_TEST) += memcpy_kunit.o
obj-$(CONFIG_MIN_HEAP_KUNIT_TEST) += min_heap_kunit.o
CFLAGS_overflow_kunit.o = $(call cc-disable-warning, tautological-constant-out-of-range-compare)
obj-$(CONFIG_OVERFLOW_KUNIT_TEST) += overflow_kunit.o
obj-$(CONFIG_PERCPU_REFCOUNT_KUNIT_TEST) += test_percpu_refcount.o
obj-$(CONFIG_PRANDOM_KUNIT_TEST) += random32_kunit.o
obj-$(CONFIG_PRINTF_KUNIT_TEST) += printf_kunit.o
obj-$(CONFIG_REFCOUNT_KUNIT_TEST) += test_refcount.o
obj-$(CONFIG_RANDSTRUCT_KUNIT_TEST) += randstruct_kunit.o
obj-$(CONFIG_SCANF_KUNIT_TEST) += scanf_kunit.o
obj-$(CONFIG_SEQ_BUF_KUNIT_TEST) += seq_buf_kunit.o
obj-$(CONFIG_SIPHASH_KUNIT_TEST) += siphash_kunit.o
obj-$(CONFIG_SLUB_KUNIT_TEST) += slub_kunit.o
obj-$(CONFIG_SWAIT_KUNIT_TEST) += test_swait.o
obj-$(CONFIG_TEST_SORT) += test_sort.o
CFLAGS_stackinit_kunit.o += $(call cc-disable-warning, switch-unreachable)
obj-$(CONFIG_STACKINIT_KUNIT_TEST) += stackinit_kunit.o
obj-$(CONFIG_STRING_KUNIT_TEST) += string_kunit.o
obj-$(CONFIG_STRING_HELPERS_KUNIT_TEST) += string_helpers_kunit.o
obj-$(CONFIG_USERCOPY_KUNIT_TEST) += usercopy_kunit.o
obj-$(CONFIG_UTIL_MACROS_KUNIT) += util_macros_kunit.o
obj-$(CONFIG_RATELIMIT_KUNIT_TEST) += test_ratelimit.o
obj-$(CONFIG_UUID_KUNIT_TEST) += uuid_kunit.o

obj-$(CONFIG_TEST_RUNTIME_MODULE)		+= module/
