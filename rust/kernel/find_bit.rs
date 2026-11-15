// SPDX-License-Identifier: GPL-2.0-only

//! Native implementation of the generic bit search algorithms.
//!
//! Rust owns the production word traversal, masking, counting, nth-bit
//! selection, clump extraction, and random-bit selection. The C entry points
//! retain the existing exported ABI and only forward to these functions.
//! Architecture-specific bitmap storage remains an `unsigned long` array.

use crate::bindings;
use core::ptr;
use macros::export;

type Word = bindings::u_long;

const BITS: usize = bindings::BITS_PER_LONG as usize;
const BYTE_BITS: usize = 8;

const ONE: u8 = 0;
const AND: u8 = 1;
const AND_NOT: u8 = 2;
const AND_AND: u8 = 3;
const NOT: u8 = 4;
const OR: u8 = 5;
const AND_AND_NOT: u8 = 6;

/// Returns a mask for the valid low bits in a partial word.
#[inline(always)]
fn last_word_mask(bits: usize) -> Word {
    let tail = bits % BITS;

    if tail == 0 {
        Word::MAX
    } else {
        (1 << tail) - 1
    }
}

/// Reads and combines one source word.
///
/// # Safety
///
/// Every non-NULL source must reference an array long enough to include
/// `index`.
#[inline(always)]
unsafe fn source_word<const OP: u8>(
    addr1: *const Word,
    addr2: *const Word,
    addr3: *const Word,
    index: usize,
) -> Word {
    // SAFETY: The caller guarantees every source used by OP is valid at index.
    unsafe {
        match OP {
            ONE => ptr::read(addr1.add(index)),
            AND => ptr::read(addr1.add(index)) & ptr::read(addr2.add(index)),
            AND_NOT => ptr::read(addr1.add(index)) & !ptr::read(addr2.add(index)),
            AND_AND => {
                ptr::read(addr1.add(index))
                    & ptr::read(addr2.add(index))
                    & ptr::read(addr3.add(index))
            }
            NOT => !ptr::read(addr1.add(index)),
            OR => ptr::read(addr1.add(index)) | ptr::read(addr2.add(index)),
            AND_AND_NOT => {
                ptr::read(addr1.add(index))
                    & ptr::read(addr2.add(index))
                    & !ptr::read(addr3.add(index))
            }
            _ => core::hint::unreachable_unchecked(),
        }
    }
}

/// Finds the first matching bit.
///
/// # Safety
///
/// The source pointers required by `OP` must be valid for `size` bits.
#[inline(always)]
unsafe fn find_first<const OP: u8>(
    addr1: *const Word,
    addr2: *const Word,
    addr3: *const Word,
    size: usize,
) -> usize {
    let mut index = 0;

    while index * BITS < size {
        // SAFETY: The loop bound keeps the word index inside the bitmap.
        let word = unsafe { source_word::<OP>(addr1, addr2, addr3, index) };
        if word != 0 {
            let bit = index * BITS + word.trailing_zeros() as usize;
            return core::cmp::min(bit, size);
        }
        index += 1;
    }

    size
}

/// Finds the first matching bit at or after `start`.
///
/// # Safety
///
/// The source pointers required by `OP` must be valid for `size` bits.
#[inline(always)]
unsafe fn find_next<const OP: u8>(
    addr1: *const Word,
    addr2: *const Word,
    addr3: *const Word,
    size: usize,
    start: usize,
) -> usize {
    if start >= size {
        return size;
    }

    let mut index = start / BITS;
    // SAFETY: `start < size` ensures that `index` is inside the bitmap.
    let mut word = unsafe { source_word::<OP>(addr1, addr2, addr3, index) };
    word &= Word::MAX << (start % BITS);

    loop {
        if word != 0 {
            let bit = index * BITS + word.trailing_zeros() as usize;
            return core::cmp::min(bit, size);
        }

        index += 1;
        if index * BITS >= size {
            return size;
        }

        // SAFETY: The check above keeps `index` inside the bitmap.
        word = unsafe { source_word::<OP>(addr1, addr2, addr3, index) };
    }
}

/// Selects the `n`th set bit in one word.
#[inline(always)]
fn nth_set_bit(word: Word, n: usize) -> usize {
    let mut low = 0;
    let mut high = BITS;

    while low + 1 < high {
        let middle = (low + high) / 2;
        let lower = word & ((1 << middle) - 1);

        if n < lower.count_ones() as usize {
            high = middle;
        } else {
            low = middle;
        }
    }

    low
}

/// Finds the `n`th matching bit.
///
/// # Safety
///
/// The source pointers required by `OP` must be valid for `size` bits.
#[inline(always)]
unsafe fn find_nth<const OP: u8>(
    addr1: *const Word,
    addr2: *const Word,
    addr3: *const Word,
    size: usize,
    mut n: usize,
) -> usize {
    let mut index = 0;

    while index * BITS < size {
        // SAFETY: The loop bound keeps the word index inside the bitmap.
        let mut word = unsafe { source_word::<OP>(addr1, addr2, addr3, index) };
        if (index + 1) * BITS > size {
            word &= last_word_mask(size);
        }

        let count = word.count_ones() as usize;
        if n < count {
            return index * BITS + nth_set_bit(word, n);
        }

        n -= count;
        index += 1;
    }

    size
}

/// Counts set bits while honoring the logical bitmap size.
///
/// # Safety
///
/// `addr` must be valid for `size` bits.
unsafe fn weight(addr: *const Word, size: usize) -> u32 {
    let mut index = 0;
    let mut count = 0u32;

    while index * BITS < size {
        // SAFETY: The loop bound keeps the word index inside the bitmap.
        let mut word = unsafe { ptr::read(addr.add(index)) };
        if (index + 1) * BITS > size {
            word &= last_word_mask(size);
        }
        count = count.wrapping_add(word.count_ones());
        index += 1;
    }

    count
}

/// Finds the first set bit.
///
/// # Safety
///
/// `addr` must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_first_bit(addr: *const Word, size: Word) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_first::<ONE>(addr, ptr::null(), ptr::null(), size) }
}

/// Finds the first bit set in both inputs.
///
/// # Safety
///
/// Both inputs must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_first_and_bit(
    addr1: *const Word,
    addr2: *const Word,
    size: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_first::<AND>(addr1, addr2, ptr::null(), size) }
}

/// Finds the first bit set in the first input and clear in the second.
///
/// # Safety
///
/// Both inputs must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_first_andnot_bit(
    addr1: *const Word,
    addr2: *const Word,
    size: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_first::<AND_NOT>(addr1, addr2, ptr::null(), size) }
}

/// Finds the first bit set in all three inputs.
///
/// # Safety
///
/// All inputs must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_first_and_and_bit(
    addr1: *const Word,
    addr2: *const Word,
    addr3: *const Word,
    size: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_first::<AND_AND>(addr1, addr2, addr3, size) }
}

/// Finds the first clear bit.
///
/// # Safety
///
/// `addr` must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_first_zero_bit(addr: *const Word, size: Word) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_first::<NOT>(addr, ptr::null(), ptr::null(), size) }
}

/// Finds the next set bit.
///
/// # Safety
///
/// `addr` must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_next_bit(addr: *const Word, size: Word, start: Word) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_next::<ONE>(addr, ptr::null(), ptr::null(), size, start) }
}

/// Finds the next bit set in both inputs.
///
/// # Safety
///
/// Both inputs must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_next_and_bit(
    addr1: *const Word,
    addr2: *const Word,
    size: Word,
    start: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_next::<AND>(addr1, addr2, ptr::null(), size, start) }
}

/// Finds the next bit set in the first input and clear in the second.
///
/// # Safety
///
/// Both inputs must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_next_andnot_bit(
    addr1: *const Word,
    addr2: *const Word,
    size: Word,
    start: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_next::<AND_NOT>(addr1, addr2, ptr::null(), size, start) }
}

/// Finds the next bit set in either input.
///
/// # Safety
///
/// Both inputs must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_next_or_bit(
    addr1: *const Word,
    addr2: *const Word,
    size: Word,
    start: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_next::<OR>(addr1, addr2, ptr::null(), size, start) }
}

/// Finds the next clear bit.
///
/// # Safety
///
/// `addr` must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_next_zero_bit(addr: *const Word, size: Word, start: Word) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_next::<NOT>(addr, ptr::null(), ptr::null(), size, start) }
}

/// Finds the `n`th set bit.
///
/// # Safety
///
/// `addr` must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_nth_bit(addr: *const Word, size: Word, n: Word) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_nth::<ONE>(addr, ptr::null(), ptr::null(), size, n) }
}

/// Finds the `n`th bit set in both inputs.
///
/// # Safety
///
/// Both inputs must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_nth_and_bit(
    addr1: *const Word,
    addr2: *const Word,
    size: Word,
    n: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_nth::<AND>(addr1, addr2, ptr::null(), size, n) }
}

/// Finds the `n`th bit set in the first two inputs and clear in the third.
///
/// # Safety
///
/// All inputs must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_nth_and_andnot_bit(
    addr1: *const Word,
    addr2: *const Word,
    addr3: *const Word,
    size: Word,
    n: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_nth::<AND_AND_NOT>(addr1, addr2, addr3, size, n) }
}

/// Finds the last set bit.
///
/// # Safety
///
/// `addr` must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_last_bit(addr: *const Word, size: Word) -> Word {
    if size == 0 {
        return 0;
    }

    let mut index = (size - 1) / BITS;
    loop {
        // SAFETY: The initial index and loop bound keep the read inside the bitmap.
        let mut word = unsafe { ptr::read(addr.add(index)) };
        if (index + 1) * BITS > size {
            word &= last_word_mask(size);
        }
        if word != 0 {
            return index * BITS + BITS - 1 - word.leading_zeros() as usize;
        }
        if index == 0 {
            break;
        }
        index -= 1;
    }

    size
}

/// Finds the next byte-sized clump that contains a set bit.
///
/// # Safety
///
/// `addr` must be valid for `size` bits and `clump` must be writable.
#[export]
unsafe extern "C" fn rust_find_next_clump8(
    clump: *mut Word,
    addr: *const Word,
    size: Word,
    offset: Word,
) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    let mut found = unsafe { find_next::<ONE>(addr, ptr::null(), ptr::null(), size, offset) };
    if found == size {
        return size;
    }

    found &= !(BYTE_BITS - 1);
    // SAFETY: A set bit exists at or after `found`, so the byte containing it
    // is within the bitmap storage. `addr` has byte alignment on x86_64.
    let value = unsafe { ptr::read(addr.cast::<u8>().add(found / BYTE_BITS)) };
    // SAFETY: The caller guarantees `clump` is writable.
    unsafe { ptr::write(clump, value as Word) };

    found
}

/// Finds a random set bit.
///
/// # Safety
///
/// `addr` must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_find_random_bit(addr: *const Word, size: Word) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    let count = unsafe { weight(addr, size) };

    match count {
        0 => size,
        1 => {
            // SAFETY: Guaranteed by this function's contract.
            unsafe { find_first::<ONE>(addr, ptr::null(), ptr::null(), size) }
        }
        _ => {
            // SAFETY: The kernel random helper accepts a non-zero ceiling.
            let n = unsafe { bindings::__get_random_u32_below(count) };
            // SAFETY: n is smaller than the set-bit count within `size`.
            unsafe { find_nth::<ONE>(addr, ptr::null(), ptr::null(), size, n as usize) }
        }
    }
}
