// SPDX-License-Identifier: GPL-2.0

//! Native pending signal selection and dequeue.
//!
//! Rust owns signal-number selection, local versus shared queue priority,
//! synchronous queue selection, stop-dequeued tracking, and POSIX timer retry
//! decisions. C helpers provide intrusive queue traversal, signal collection,
//! pending-state recalculation, and timer operations.

use crate::bindings;
use core::ptr;
use macros::export;

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Info = bindings::kernel_siginfo;
type Pending = bindings::sigpending;
type Queue = bindings::sigqueue;
type SignalSet = bindings::sigset_t;
type Task = bindings::task_struct;

const JOBCTL_STOP_DEQUEUED: usize = bindings::JOBCTL_STOP_DEQUEUED as usize;
const PIDTYPE_PID: u32 = bindings::pid_type_PIDTYPE_PID;
const PIDTYPE_TGID: u32 = bindings::pid_type_PIDTYPE_TGID;
const SI_USER: i32 = bindings::SI_USER as i32;
const SIGALRM: i32 = bindings::SIGALRM as i32;

const fn signal_mask(sig: u32) -> usize {
    let bit = sig as usize;
    if bit == 0 || bit > usize::BITS as usize {
        0
    } else {
        1usize << (bit - 1)
    }
}

const SYNCHRONOUS_MASK: usize = signal_mask(bindings::SIGSEGV)
    | signal_mask(bindings::SIGBUS)
    | signal_mask(bindings::SIGILL)
    | signal_mask(bindings::SIGTRAP)
    | signal_mask(bindings::SIGFPE)
    | signal_mask(bindings::SIGSYS);

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static NEXT_ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static NEXT_FIRST_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static NEXT_SYNCHRONOUS_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static DEQUEUE_ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static LOCAL_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static SHARED_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static EMPTY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static STOP_MARKER_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static TIMER_RETRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static DELIVER_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static SYNCHRONOUS_ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static SYNCHRONOUS_PRECHECK_MISS_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static SYNCHRONOUS_SCAN_MISS_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static SYNCHRONOUS_DUPLICATE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static SYNCHRONOUS_CLEAR_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
static SYNCHRONOUS_DELIVER_COUNT: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_reset() {
    NEXT_ENTRY_COUNT.store(0, Ordering::Relaxed);
    NEXT_FIRST_COUNT.store(0, Ordering::Relaxed);
    NEXT_SYNCHRONOUS_COUNT.store(0, Ordering::Relaxed);
    DEQUEUE_ENTRY_COUNT.store(0, Ordering::Relaxed);
    LOCAL_COUNT.store(0, Ordering::Relaxed);
    SHARED_COUNT.store(0, Ordering::Relaxed);
    EMPTY_COUNT.store(0, Ordering::Relaxed);
    STOP_MARKER_COUNT.store(0, Ordering::Relaxed);
    TIMER_RETRY_COUNT.store(0, Ordering::Relaxed);
    DELIVER_COUNT.store(0, Ordering::Relaxed);
    SYNCHRONOUS_ENTRY_COUNT.store(0, Ordering::Relaxed);
    SYNCHRONOUS_PRECHECK_MISS_COUNT.store(0, Ordering::Relaxed);
    SYNCHRONOUS_SCAN_MISS_COUNT.store(0, Ordering::Relaxed);
    SYNCHRONOUS_DUPLICATE_COUNT.store(0, Ordering::Relaxed);
    SYNCHRONOUS_CLEAR_COUNT.store(0, Ordering::Relaxed);
    SYNCHRONOUS_DELIVER_COUNT.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_next_entry_count() -> u32 {
    NEXT_ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_next_first_count() -> u32 {
    NEXT_FIRST_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_next_synchronous_count() -> u32 {
    NEXT_SYNCHRONOUS_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_dequeue_entry_count() -> u32 {
    DEQUEUE_ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_local_count() -> u32 {
    LOCAL_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_shared_count() -> u32 {
    SHARED_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_empty_count() -> u32 {
    EMPTY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_stop_marker_count() -> u32 {
    STOP_MARKER_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_timer_retry_count() -> u32 {
    TIMER_RETRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_deliver_count() -> u32 {
    DELIVER_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_synchronous_entry_count() -> u32 {
    SYNCHRONOUS_ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_synchronous_precheck_miss_count() -> u32 {
    SYNCHRONOUS_PRECHECK_MISS_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_synchronous_scan_miss_count() -> u32 {
    SYNCHRONOUS_SCAN_MISS_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_synchronous_duplicate_count() -> u32 {
    SYNCHRONOUS_DUPLICATE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_synchronous_clear_count() -> u32 {
    SYNCHRONOUS_CLEAR_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_dequeue_test_synchronous_deliver_count() -> u32 {
    SYNCHRONOUS_DELIVER_COUNT.load(Ordering::Relaxed)
}

fn signal_clear(set: &mut SignalSet, sig: u32) {
    let mask = signal_mask(sig);
    if mask == 0 {
        return;
    }

    let word = sig as usize - 1;
    let shift = word % usize::BITS as usize;
    set.sig[word / usize::BITS as usize] &= !(1usize << shift);
}

/// Selects the first unblocked pending signal and its selection class.
fn select_next(pending: &SignalSet, mask: &SignalSet) -> (i32, bool, bool) {
    for (index, (pending, blocked)) in pending.sig.iter().zip(mask.sig.iter()).enumerate() {
        let mut available = *pending & !*blocked;
        let first = index == 0;
        let synchronous = first && available & SYNCHRONOUS_MASK != 0;
        if synchronous {
            available &= SYNCHRONOUS_MASK;
        }
        if available != 0 {
            let bit = index * usize::BITS as usize + available.trailing_zeros() as usize;
            return ((bit + 1) as i32, first, synchronous);
        }
    }

    (0, false, false)
}

/// Collects downgraded queue information for a selected signal.
///
/// # Safety
///
/// The pending queue and output pointers must be live under the signal lock.
unsafe fn dequeue_from_pending(
    pending: *mut Pending,
    mask: *mut SignalSet,
    info: *mut Info,
) -> (i32, *mut Queue) {
    // SAFETY: The caller guarantees live queue and mask storage.
    let (sig, _, _) = select_next(unsafe { &(*pending).signal }, unsafe { &*mask });
    let mut timer = ptr::null_mut();

    if sig != 0 {
        // SAFETY: The queue, information buffer, and signal lock are live.
        unsafe { bindings::rust_helper_signal_dequeue_collect(pending, sig, info, &mut timer) };
    }

    (sig, timer)
}

/// Selects and dequeues a signal for the current task.
///
/// # Safety
///
/// `task` must be `current`, all pointers must be valid, and the caller must
/// hold the signal-handler lock with interrupts disabled.
unsafe fn dequeue_signal(
    task: *mut Task,
    mask: *mut SignalSet,
    info: *mut Info,
    pid_type: *mut u32,
) -> i32 {
    #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
    DEQUEUE_ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    loop {
        // SAFETY: The signal lock keeps the task's queues live.
        unsafe { *pid_type = PIDTYPE_PID };
        // SAFETY: The local queue and output buffers are live under the lock.
        let (local, mut timer) =
            unsafe { dequeue_from_pending(ptr::addr_of_mut!((*task).pending), mask, info) };

        let sig = if local != 0 {
            #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
            LOCAL_COUNT.fetch_add(1, Ordering::Relaxed);
            local
        } else {
            #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
            SHARED_COUNT.fetch_add(1, Ordering::Relaxed);
            // SAFETY: The caller supplied valid writable pid-type storage.
            unsafe { *pid_type = PIDTYPE_TGID };
            // SAFETY: The shared queue is live under the signal-handler lock.
            let result = unsafe {
                dequeue_from_pending(
                    ptr::addr_of_mut!((*(*task).signal).shared_pending),
                    mask,
                    info,
                )
            };
            timer = result.1;
            if result.0 == SIGALRM {
                // SAFETY: The current task is live and the signal lock is held.
                unsafe { bindings::rust_helper_signal_dequeue_rearm_itimer(task) };
            }
            result.0
        };

        // SAFETY: Recalculation updates current, which is the supplied task.
        unsafe { bindings::rust_helper_signal_dequeue_recalc() };

        if sig == 0 {
            #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
            EMPTY_COUNT.fetch_add(1, Ordering::Relaxed);
            return 0;
        }

        // SAFETY: The current task and signal classification are valid.
        if unsafe { bindings::rust_helper_signal_dequeue_is_kernel_stop(sig) } {
            // SAFETY: jobctl is protected by the signal-handler lock.
            unsafe { (*task).jobctl |= JOBCTL_STOP_DEQUEUED };
            #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
            STOP_MARKER_COUNT.fetch_add(1, Ordering::Relaxed);
        }

        #[cfg(CONFIG_POSIX_TIMERS)]
        if !timer.is_null() {
            // SAFETY: The timer queue owns a live reference to the timer.
            if unsafe { !bindings::rust_helper_signal_dequeue_deliver_timer(info, timer) } {
                #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
                TIMER_RETRY_COUNT.fetch_add(1, Ordering::Relaxed);
                continue;
            }
        }

        #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
        DELIVER_COUNT.fetch_add(1, Ordering::Relaxed);
        return sig;
    }
}

/// Selects and dequeues a synchronous signal for the current task.
///
/// # Safety
///
/// `task` must be `current`, `info` must be valid, and the caller must hold the
/// signal-handler lock with interrupts disabled.
unsafe fn dequeue_synchronous_signal(task: *mut Task, info: *mut Info) -> i32 {
    #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
    SYNCHRONOUS_ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The task and pending queue are live under the signal-handler lock.
    let pending = unsafe { ptr::addr_of_mut!((*task).pending) };
    // SAFETY: The pending and blocked sets are protected by the signal lock.
    let available = unsafe { (*pending).signal.sig[0] & !(*task).blocked.sig[0] };
    if available & SYNCHRONOUS_MASK == 0 {
        #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
        SYNCHRONOUS_PRECHECK_MISS_COUNT.fetch_add(1, Ordering::Relaxed);
        return 0;
    }

    // SAFETY: The pending queue is live under the signal-handler lock.
    let mut queue = unsafe { bindings::rust_helper_signal_dequeue_first(pending) };
    let mut synchronous = ptr::null_mut();
    while !queue.is_null() {
        // SAFETY: Queue entries are live under the signal-handler lock.
        let queue_info = unsafe { &(*queue).info };
        if queue_info.__bindgen_anon_1.si_code > SI_USER
            && signal_mask(queue_info.__bindgen_anon_1.si_signo as u32) & SYNCHRONOUS_MASK != 0
        {
            synchronous = queue;
            break;
        }

        // SAFETY: The queue remains live under the signal-handler lock.
        queue = unsafe { bindings::rust_helper_signal_dequeue_next(queue, pending) };
    }

    if synchronous.is_null() {
        #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
        SYNCHRONOUS_SCAN_MISS_COUNT.fetch_add(1, Ordering::Relaxed);
        return 0;
    }

    // SAFETY: Queue entries and their embedded siginfo are live under the lock.
    let sig = unsafe { (*synchronous).info.__bindgen_anon_1.si_signo };
    let mut duplicate = false;
    // SAFETY: The queue remains live under the signal-handler lock.
    let mut other = unsafe { bindings::rust_helper_signal_dequeue_next(synchronous, pending) };
    while !other.is_null() {
        // SAFETY: Queue entries are live under the signal-handler lock.
        if unsafe { (*other).info.__bindgen_anon_1.si_signo } == sig {
            duplicate = true;
            break;
        }
        // SAFETY: The queue remains live under the signal-handler lock.
        other = unsafe { bindings::rust_helper_signal_dequeue_next(other, pending) };
    }

    if duplicate {
        #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
        SYNCHRONOUS_DUPLICATE_COUNT.fetch_add(1, Ordering::Relaxed);
    } else {
        // SAFETY: The pending set is writable under the signal-handler lock.
        unsafe { signal_clear(&mut (*pending).signal, sig as u32) };
        // SAFETY: Recalculation updates current, which is the supplied task.
        unsafe { bindings::rust_helper_signal_dequeue_recalc() };
        #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
        SYNCHRONOUS_CLEAR_COUNT.fetch_add(1, Ordering::Relaxed);
    }

    // SAFETY: The selected queue entry is live and owned by the pending queue.
    unsafe { bindings::rust_helper_signal_dequeue_remove_sync(synchronous, info) };
    #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
    SYNCHRONOUS_DELIVER_COUNT.fetch_add(1, Ordering::Relaxed);
    sig
}

/// Selects the first unblocked pending signal.
///
/// # Safety
///
/// The pending queue and mask must remain live for the duration of the call.
#[export]
unsafe extern "C" fn rust_signal_next_pending(pending: *mut Pending, mask: *mut SignalSet) -> i32 {
    #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
    NEXT_ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller guarantees live pending and mask storage.
    let (sig, first, synchronous) = select_next(unsafe { &(*pending).signal }, unsafe { &*mask });
    #[cfg(not(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST))]
    let _ = (first, synchronous);
    #[cfg(CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST)]
    if sig != 0 {
        if first {
            NEXT_FIRST_COUNT.fetch_add(1, Ordering::Relaxed);
        }
        if synchronous {
            NEXT_SYNCHRONOUS_COUNT.fetch_add(1, Ordering::Relaxed);
        }
    }
    sig
}

/// Dequeues a pending signal for `current`.
///
/// # Safety
///
/// The requirements of [`dequeue_signal`] apply.
#[export]
unsafe extern "C" fn rust_signal_dequeue(
    task: *mut Task,
    mask: *mut SignalSet,
    info: *mut Info,
    pid_type: *mut u32,
) -> i32 {
    // SAFETY: The caller upholds the function requirements.
    unsafe { dequeue_signal(task, mask, info, pid_type) }
}

/// Dequeues a synchronous signal for `current`.
///
/// # Safety
///
/// The requirements of [`dequeue_synchronous_signal`] apply.
#[export]
unsafe extern "C" fn rust_signal_dequeue_synchronous(task: *mut Task, info: *mut Info) -> i32 {
    // SAFETY: The caller upholds the function requirements.
    unsafe { dequeue_synchronous_signal(task, info) }
}
