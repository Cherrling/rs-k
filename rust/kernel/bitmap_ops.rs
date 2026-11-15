// SPDX-License-Identifier: GPL-2.0-only

//! Native implementation of the generic bitmap algorithms.
//!
//! Rust owns word traversal, masks, logical bitmap operations, population
//! counts, range manipulation, mapping, and array conversion. The C entry
//! points retain the existing ABI and only forward to these functions.

use crate::bindings;
use crate::find_bit;
use core::ffi::c_int;
use core::ptr;
use macros::export;

type Word = bindings::u_long;

const BITS: usize = bindings::BITS_PER_LONG as usize;

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

/// Returns the number of storage words required for `bits`.
#[inline(always)]
fn words(bits: usize) -> usize {
    bits.div_ceil(BITS)
}

/// Reads one bitmap word.
///
/// # Safety
///
/// `addr` must reference a bitmap that contains `index`.
#[inline(always)]
unsafe fn read_word(addr: *const Word, index: usize) -> Word {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { ptr::read(addr.add(index)) }
}

/// Writes one bitmap word.
///
/// # Safety
///
/// `addr` must reference a writable bitmap that contains `index`.
#[inline(always)]
unsafe fn write_word(addr: *mut Word, index: usize, value: Word) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { ptr::write(addr.add(index), value) }
}

/// Returns whether `bit` is set.
///
/// # Safety
///
/// `addr` must be valid for `bit` bits.
#[inline(always)]
unsafe fn test_bit(addr: *const Word, bit: usize) -> bool {
    // SAFETY: The caller guarantees that the referenced word exists.
    unsafe { read_word(addr, bit / BITS) & (1 << (bit % BITS)) != 0 }
}

/// Sets one bit without a logical size check.
///
/// # Safety
///
/// `addr` must reference a writable bitmap that contains `bit`.
#[inline(always)]
unsafe fn set_bit(addr: *mut Word, bit: usize) {
    let index = bit / BITS;
    // SAFETY: Guaranteed by this function's contract.
    let word = unsafe { read_word(addr.cast_const(), index) };
    // SAFETY: Guaranteed by this function's contract.
    unsafe { write_word(addr, index, word | (1 << (bit % BITS))) };
}

/// Clears all storage words.
///
/// # Safety
///
/// `dst` must be writable for `bits` bits.
#[inline(always)]
unsafe fn zero(dst: *mut Word, bits: usize) {
    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps the write inside the bitmap.
        unsafe { write_word(dst, index, 0) };
    }
}

/// Counts set bits while honoring the logical bitmap size.
///
/// # Safety
///
/// `addr` must be valid for `bits` bits.
unsafe fn weight(addr: *const Word, bits: usize) -> u32 {
    let mut count = 0u32;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps the read inside the bitmap.
        let mut word = unsafe { read_word(addr, index) };
        if (index + 1) * BITS > bits {
            word &= last_word_mask(bits);
        }
        count = count.wrapping_add(word.count_ones());
    }

    count
}

/// Returns the ordinal of a set bit, or `None` for an unset or invalid bit.
///
/// # Safety
///
/// `addr` must be valid for `bits` bits.
unsafe fn position_to_ordinal(addr: *const Word, position: usize, bits: usize) -> Option<usize> {
    if position >= bits {
        return None;
    }
    // SAFETY: The position check and this function's contract keep the read valid.
    if !unsafe { test_bit(addr, position) } {
        return None;
    }
    // SAFETY: `position <= bits`, so this is a valid logical prefix.
    Some(unsafe { weight(addr, position) } as usize)
}

/// Compares two bitmaps.
///
/// # Safety
///
/// Both inputs must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_equal(
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> bool {
    let bits = bits as usize;
    let full = bits / BITS;

    for index in 0..full {
        // SAFETY: The full-word loop stays inside both bitmaps.
        if unsafe { read_word(bitmap1, index) } != unsafe { read_word(bitmap2, index) } {
            return false;
        }
    }

    if bits % BITS != 0 {
        // SAFETY: A non-zero tail implies one final word in both bitmaps.
        let diff = unsafe { read_word(bitmap1, full) ^ read_word(bitmap2, full) };
        return diff & last_word_mask(bits) == 0;
    }

    true
}

/// Tests whether the OR of two bitmaps equals a third bitmap.
///
/// # Safety
///
/// All inputs must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_or_equal(
    bitmap1: *const Word,
    bitmap2: *const Word,
    bitmap3: *const Word,
    bits: u32,
) -> bool {
    let bits = bits as usize;
    let full = bits / BITS;

    for index in 0..full {
        // SAFETY: The full-word loop stays inside all bitmaps.
        let value = unsafe { read_word(bitmap1, index) | read_word(bitmap2, index) };
        // SAFETY: The full-word loop stays inside all bitmaps.
        if value != unsafe { read_word(bitmap3, index) } {
            return false;
        }
    }

    if bits % BITS != 0 {
        // SAFETY: A non-zero tail implies one final word in all bitmaps.
        let value = unsafe { read_word(bitmap1, full) | read_word(bitmap2, full) };
        // SAFETY: A non-zero tail implies one final word in all bitmaps.
        let diff = value ^ unsafe { read_word(bitmap3, full) };
        return diff & last_word_mask(bits) == 0;
    }

    true
}

/// Complements every storage word.
///
/// # Safety
///
/// Both bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_complement(dst: *mut Word, src: *const Word, bits: u32) {
    for index in 0..words(bits as usize) {
        // SAFETY: The loop bound keeps both accesses inside the bitmaps.
        let value = !unsafe { read_word(src, index) };
        // SAFETY: The loop bound keeps the write inside the destination.
        unsafe { write_word(dst, index, value) };
    }
}

/// Shifts a bitmap right.
///
/// # Safety
///
/// Both bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_shift_right(
    dst: *mut Word,
    src: *const Word,
    shift: u32,
    bits: u32,
) {
    let bits = bits as usize;
    let shift = shift as usize;
    let limit = words(bits);

    if shift >= bits {
        // SAFETY: The destination is valid for all storage words.
        unsafe { zero(dst, bits) };
        return;
    }

    let word_shift = shift / BITS;
    let bit_shift = shift % BITS;
    let mask = last_word_mask(bits);

    for index in 0..limit - word_shift {
        let source_index = word_shift + index;
        let mut upper = 0;

        if bit_shift != 0 && source_index + 1 < limit {
            // SAFETY: The loop bound keeps the read inside the source.
            upper = unsafe { read_word(src, source_index + 1) };
            if source_index + 1 == limit - 1 {
                upper &= mask;
            }
            upper <<= BITS - bit_shift;
        }

        // SAFETY: The loop bound keeps the read inside the source.
        let mut lower = unsafe { read_word(src, source_index) };
        if source_index == limit - 1 {
            lower &= mask;
        }
        lower >>= bit_shift;
        // SAFETY: `index < limit`.
        unsafe { write_word(dst, index, lower | upper) };
    }

    if word_shift != 0 {
        let first = limit - word_shift;
        // SAFETY: The range is the tail that was not assigned by the loop.
        unsafe {
            ptr::write_bytes(dst.add(first), 0, word_shift);
        }
    }
}

/// Shifts a bitmap left.
///
/// # Safety
///
/// Both bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_shift_left(
    dst: *mut Word,
    src: *const Word,
    shift: u32,
    bits: u32,
) {
    let bits = bits as usize;
    let shift = shift as usize;
    let limit = words(bits);

    if shift >= bits {
        // SAFETY: The destination is valid for all storage words.
        unsafe { zero(dst, bits) };
        return;
    }

    let word_shift = shift / BITS;
    let bit_shift = shift % BITS;

    for source_index in (0..limit - word_shift).rev() {
        let lower = if bit_shift != 0 && source_index != 0 {
            // SAFETY: The loop bound keeps the read inside the source.
            unsafe { read_word(src, source_index - 1) >> (BITS - bit_shift) }
        } else {
            0
        };
        // SAFETY: The loop bound keeps the read inside the source.
        let upper = unsafe { read_word(src, source_index) << bit_shift };
        // SAFETY: The destination index is below `limit`.
        unsafe { write_word(dst, source_index + word_shift, lower | upper) };
    }

    if word_shift != 0 {
        // SAFETY: The prefix has `word_shift` valid destination words.
        unsafe {
            ptr::write_bytes(dst, 0, word_shift);
        }
    }
}

/// Removes a bit range and shifts the remaining high bits right.
///
/// # Safety
///
/// `dst` and `src` must be valid for `bits` bits. They may overlap.
#[export]
unsafe extern "C" fn rust_bitmap_cut(
    dst: *mut Word,
    src: *const Word,
    first: u32,
    cut: u32,
    bits: u32,
) {
    let bits = bits as usize;
    if bits == 0 {
        return;
    }

    let first = first as usize;
    let length = words(bits);
    let first_word = first / BITS;
    let first_offset = first % BITS;
    let keep = if first_offset != 0 {
        // SAFETY: The caller guarantees a valid cut position.
        unsafe { read_word(src, first_word) & (Word::MAX >> (BITS - first_offset)) }
    } else {
        0
    };

    // SAFETY: The caller guarantees complete source and destination bitmaps.
    unsafe {
        ptr::copy(src, dst, length);
    }

    for _ in 0..cut {
        for index in first_word..length {
            let carry = if index + 1 < length {
                // SAFETY: The branch condition keeps the read inside the destination.
                unsafe { read_word(dst, index + 1) & 1 }
            } else {
                0
            };
            // SAFETY: The loop bound keeps the read inside the destination.
            let value = unsafe { read_word(dst, index) >> 1 } | (carry << (BITS - 1));
            // SAFETY: The loop bound keeps the write inside the destination.
            unsafe { write_word(dst, index, value) };
        }
    }

    // SAFETY: `first_word` is valid for a valid cut position.
    let value = unsafe { read_word(dst, first_word) };
    // SAFETY: `first_word` is valid for a valid cut position.
    unsafe {
        write_word(
            dst,
            first_word,
            (value & (Word::MAX << first_offset)) | keep,
        );
    }
}

/// Computes the bitwise AND of two bitmaps.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_and(
    dst: *mut Word,
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> bool {
    let bits = bits as usize;
    let mut result = 0;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps both reads inside the source bitmaps.
        let mut value = unsafe { read_word(bitmap1, index) & read_word(bitmap2, index) };
        if (index + 1) * BITS > bits {
            value &= last_word_mask(bits);
        }
        result |= value;
        // SAFETY: The loop bound keeps the write inside the destination.
        unsafe { write_word(dst, index, value) };
    }

    result != 0
}

/// Computes the bitwise OR of two bitmaps.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_or(
    dst: *mut Word,
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) {
    for index in 0..words(bits as usize) {
        // SAFETY: The loop bound keeps both reads inside the source bitmaps.
        let value = unsafe { read_word(bitmap1, index) | read_word(bitmap2, index) };
        // SAFETY: The loop bound keeps the write inside the destination.
        unsafe { write_word(dst, index, value) };
    }
}

/// Computes the bitwise XOR of two bitmaps.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_xor(
    dst: *mut Word,
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) {
    for index in 0..words(bits as usize) {
        // SAFETY: The loop bound keeps both reads inside the source bitmaps.
        let value = unsafe { read_word(bitmap1, index) ^ read_word(bitmap2, index) };
        // SAFETY: The loop bound keeps the write inside the destination.
        unsafe { write_word(dst, index, value) };
    }
}

/// Computes the bitwise AND-NOT of two bitmaps.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_andnot(
    dst: *mut Word,
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> bool {
    let bits = bits as usize;
    let mut result = 0;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps both reads inside the source bitmaps.
        let mut value = unsafe { read_word(bitmap1, index) & !read_word(bitmap2, index) };
        if (index + 1) * BITS > bits {
            value &= last_word_mask(bits);
        }
        result |= value;
        // SAFETY: The loop bound keeps the write inside the destination.
        unsafe { write_word(dst, index, value) };
    }

    result != 0
}

/// Replaces destination bits selected by a mask.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_replace(
    dst: *mut Word,
    old: *const Word,
    new: *const Word,
    mask: *const Word,
    bits: u32,
) {
    for index in 0..words(bits as usize) {
        // SAFETY: The loop bound keeps all reads inside the source bitmaps.
        let mask_word = unsafe { read_word(mask, index) };
        // SAFETY: The loop bound keeps all reads inside the source bitmaps.
        let value = (unsafe { read_word(old, index) } & !mask_word)
            | (unsafe { read_word(new, index) } & mask_word);
        // SAFETY: The loop bound keeps the write inside the destination.
        unsafe { write_word(dst, index, value) };
    }
}

/// Tests whether two bitmaps share a set bit.
///
/// # Safety
///
/// Both inputs must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_intersects(
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> bool {
    let bits = bits as usize;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps both reads inside the inputs.
        let mut value = unsafe { read_word(bitmap1, index) & read_word(bitmap2, index) };
        if (index + 1) * BITS > bits {
            value &= last_word_mask(bits);
        }
        if value != 0 {
            return true;
        }
    }

    false
}

/// Tests whether the first bitmap is a subset of the second.
///
/// # Safety
///
/// Both inputs must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_subset(
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> bool {
    let bits = bits as usize;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps both reads inside the inputs.
        let mut value = unsafe { read_word(bitmap1, index) & !read_word(bitmap2, index) };
        if (index + 1) * BITS > bits {
            value &= last_word_mask(bits);
        }
        if value != 0 {
            return false;
        }
    }

    true
}

/// Counts the set bits in a bitmap.
///
/// # Safety
///
/// `bitmap` must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_weight(bitmap: *const Word, bits: u32) -> u32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { weight(bitmap, bits as usize) }
}

/// Counts the set bits in the intersection of two bitmaps.
///
/// # Safety
///
/// Both inputs must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_weight_and(
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> u32 {
    let bits = bits as usize;
    let mut count = 0u32;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps both reads inside the inputs.
        let mut value = unsafe { read_word(bitmap1, index) & read_word(bitmap2, index) };
        if (index + 1) * BITS > bits {
            value &= last_word_mask(bits);
        }
        count = count.wrapping_add(value.count_ones());
    }

    count
}

/// Counts the set bits in an AND-NOT bitmap.
///
/// # Safety
///
/// Both inputs must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_weight_andnot(
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> u32 {
    let bits = bits as usize;
    let mut count = 0u32;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps both reads inside the inputs.
        let mut value = unsafe { read_word(bitmap1, index) & !read_word(bitmap2, index) };
        if (index + 1) * BITS > bits {
            value &= last_word_mask(bits);
        }
        count = count.wrapping_add(value.count_ones());
    }

    count
}

/// Stores the OR of two bitmaps and returns its set-bit count.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_weighted_or(
    dst: *mut Word,
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> u32 {
    let bits = bits as usize;
    let mut count = 0u32;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps both reads inside the source bitmaps.
        let value = unsafe { read_word(bitmap1, index) | read_word(bitmap2, index) };
        let counted = if (index + 1) * BITS > bits {
            value & last_word_mask(bits)
        } else {
            value
        };
        count = count.wrapping_add(counted.count_ones());
        // SAFETY: The loop bound keeps the write inside the destination.
        unsafe { write_word(dst, index, value) };
    }

    count
}

/// Stores the XOR of two bitmaps and returns its set-bit count.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_weighted_xor(
    dst: *mut Word,
    bitmap1: *const Word,
    bitmap2: *const Word,
    bits: u32,
) -> u32 {
    let bits = bits as usize;
    let mut count = 0u32;

    for index in 0..words(bits) {
        // SAFETY: The loop bound keeps both reads inside the source bitmaps.
        let value = unsafe { read_word(bitmap1, index) ^ read_word(bitmap2, index) };
        let counted = if (index + 1) * BITS > bits {
            value & last_word_mask(bits)
        } else {
            value
        };
        count = count.wrapping_add(counted.count_ones());
        // SAFETY: The loop bound keeps the write inside the destination.
        unsafe { write_word(dst, index, value) };
    }

    count
}

/// Sets a contiguous bit range.
///
/// # Safety
///
/// `map` must be writable for the complete range.
#[export]
unsafe extern "C" fn rust_bitmap_set(map: *mut Word, start: u32, len: c_int) {
    if len <= 0 {
        return;
    }

    let start = start as usize;
    let end = start.wrapping_add(len as usize);
    let mut remaining = len as usize;
    let mut index = start / BITS;
    let mut bits_to_set = BITS - start % BITS;
    let mut mask = Word::MAX << (start % BITS);

    while remaining >= bits_to_set {
        // SAFETY: The caller guarantees a writable bitmap for the range.
        let value = unsafe { read_word(map.cast_const(), index) } | mask;
        // SAFETY: The caller guarantees a writable bitmap for the range.
        unsafe { write_word(map, index, value) };
        remaining -= bits_to_set;
        bits_to_set = BITS;
        mask = Word::MAX;
        index += 1;
    }

    if remaining != 0 {
        mask &= last_word_mask(end);
        // SAFETY: A remaining tail implies this final word is valid.
        let value = unsafe { read_word(map.cast_const(), index) } | mask;
        // SAFETY: A remaining tail implies this final word is valid.
        unsafe { write_word(map, index, value) };
    }
}

/// Clears a contiguous bit range.
///
/// # Safety
///
/// `map` must be writable for the complete range.
#[export]
unsafe extern "C" fn rust_bitmap_clear(map: *mut Word, start: u32, len: c_int) {
    if len <= 0 {
        return;
    }

    let start = start as usize;
    let end = start.wrapping_add(len as usize);
    let mut remaining = len as usize;
    let mut index = start / BITS;
    let mut bits_to_clear = BITS - start % BITS;
    let mut mask = Word::MAX << (start % BITS);

    while remaining >= bits_to_clear {
        // SAFETY: The caller guarantees a writable bitmap for the range.
        let value = unsafe { read_word(map.cast_const(), index) } & !mask;
        // SAFETY: The caller guarantees a writable bitmap for the range.
        unsafe { write_word(map, index, value) };
        remaining -= bits_to_clear;
        bits_to_clear = BITS;
        mask = Word::MAX;
        index += 1;
    }

    if remaining != 0 {
        mask &= last_word_mask(end);
        // SAFETY: A remaining tail implies this final word is valid.
        let value = unsafe { read_word(map.cast_const(), index) } & !mask;
        // SAFETY: A remaining tail implies this final word is valid.
        unsafe { write_word(map, index, value) };
    }
}

/// Finds a contiguous aligned range of clear bits.
///
/// # Safety
///
/// `map` must be valid for `size` bits.
#[export]
unsafe extern "C" fn rust_bitmap_find_next_zero_area_off(
    map: *const Word,
    size: Word,
    start: Word,
    nr: u32,
    align_mask: Word,
    align_offset: Word,
) -> Word {
    let mut cursor = start;
    let nr = nr as usize;

    while cursor < size {
        // SAFETY: The caller guarantees a bitmap of `size` bits.
        cursor = unsafe { find_bit::find_next_clear(map, size, cursor) };
        if cursor >= size {
            break;
        }

        cursor = cursor.wrapping_add(align_offset).wrapping_add(align_mask) & !align_mask;
        cursor = cursor.wrapping_sub(align_offset);

        let Some(end) = cursor.checked_add(nr) else {
            break;
        };
        if end > size {
            break;
        }

        let word_start = cursor & !(BITS - 1);
        // SAFETY: `cursor` and `end` are within the logically valid bitmap.
        let found = unsafe { find_bit::find_last_set(map.add(cursor / BITS), end - word_start) }
            + word_start;
        if found >= end || found < cursor {
            return cursor as Word;
        }

        cursor = found + 1;
    }

    size as Word
}

/// Applies a positional bitmap mapping to a source bitmap.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_remap(
    dst: *mut Word,
    src: *const Word,
    old: *const Word,
    new: *const Word,
    bits: u32,
) {
    if dst.cast_const() == src {
        return;
    }

    let bits = bits as usize;
    // SAFETY: The destination is valid for `bits` bits.
    unsafe { zero(dst, bits) };
    // SAFETY: The mapped bitmap is valid for `bits` bits.
    let new_weight = unsafe { weight(new, bits) } as usize;

    let mut oldbit = 0;
    loop {
        // SAFETY: All source bitmaps are valid for `bits` bits.
        oldbit = unsafe { find_bit::find_next_set(src, bits, oldbit) };
        if oldbit >= bits {
            break;
        }

        // SAFETY: The mapping bitmap is valid for `bits` bits.
        match unsafe { position_to_ordinal(old, oldbit, bits) } {
            None => {
                // SAFETY: `oldbit < bits`.
                unsafe { set_bit(dst, oldbit) };
            }
            Some(_) if new_weight == 0 => {
                // SAFETY: `oldbit < bits`.
                unsafe { set_bit(dst, oldbit) };
            }
            Some(ordinal) => {
                // SAFETY: The nth search is bounded by `bits`.
                let target = unsafe { find_bit::find_nth_set(new, bits, ordinal % new_weight) };
                // SAFETY: A successful nth search is below `bits`.
                unsafe { set_bit(dst, target) };
            }
        }

        oldbit += 1;
    }
}

/// Maps one bit position through a pair of mapping bitmaps.
///
/// # Safety
///
/// Both mapping bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_bitremap(
    oldbit: c_int,
    old: *const Word,
    new: *const Word,
    bits: c_int,
) -> c_int {
    if oldbit < 0 || bits < 0 {
        return oldbit;
    }

    let bits = bits as usize;
    // SAFETY: The mapping bitmap is valid for `bits` bits.
    let new_weight = unsafe { weight(new, bits) } as usize;
    // SAFETY: The mapping bitmap is valid for `bits` bits.
    let Some(ordinal) = (unsafe { position_to_ordinal(old, oldbit as usize, bits) }) else {
        return oldbit;
    };
    if new_weight == 0 {
        return oldbit;
    }

    // SAFETY: The mapping bitmap is valid for `bits` bits.
    unsafe { find_bit::find_nth_set(new, bits, ordinal % new_weight) as c_int }
}

/// Translates a bitmap relative to a reference bitmap.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_onto(
    dst: *mut Word,
    orig: *const Word,
    relmap: *const Word,
    bits: u32,
) {
    if dst.cast_const() == orig {
        return;
    }

    let bits = bits as usize;
    // SAFETY: The destination is valid for `bits` bits.
    unsafe { zero(dst, bits) };

    let mut ordinal = 0;
    let mut target = 0;
    loop {
        // SAFETY: The reference bitmap is valid for `bits` bits.
        target = unsafe { find_bit::find_next_set(relmap, bits, target) };
        if target >= bits {
            break;
        }

        if ordinal < bits {
            // SAFETY: The source and destination bitmaps are valid for `bits` bits.
            if unsafe { test_bit(orig, ordinal) } {
                // SAFETY: `target < bits`.
                unsafe { set_bit(dst, target) };
            }
        }

        ordinal += 1;
        target += 1;
    }
}

/// Folds a bitmap modulo a smaller size.
///
/// # Safety
///
/// Both bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_fold(dst: *mut Word, orig: *const Word, size: u32, bits: u32) {
    if dst.cast_const() == orig || size == 0 {
        return;
    }

    let bits = bits as usize;
    // SAFETY: The destination is valid for `bits` bits.
    unsafe { zero(dst, bits) };

    let mut oldbit = 0;
    loop {
        // SAFETY: The source bitmap is valid for `bits` bits.
        oldbit = unsafe { find_bit::find_next_set(orig, bits, oldbit) };
        if oldbit >= bits {
            break;
        }
        // SAFETY: The modulo result is below `bits`.
        unsafe { set_bit(dst, oldbit % size as usize) };
        oldbit += 1;
    }
}

/// Scatters dense bits into positions selected by a mask.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_scatter(
    dst: *mut Word,
    src: *const Word,
    mask: *const Word,
    bits: u32,
) {
    let bits = bits as usize;
    // SAFETY: The destination is valid for `bits` bits.
    unsafe { zero(dst, bits) };

    let mut source_bit = 0;
    let mut mask_bit = 0;
    loop {
        // SAFETY: The mask bitmap is valid for `bits` bits.
        mask_bit = unsafe { find_bit::find_next_set(mask, bits, mask_bit) };
        if mask_bit >= bits {
            break;
        }

        // SAFETY: `source_bit < bits` because every mask bit has a unique ordinal.
        if unsafe { test_bit(src, source_bit) } {
            // SAFETY: `mask_bit < bits`.
            unsafe { set_bit(dst, mask_bit) };
        }
        source_bit += 1;
        mask_bit += 1;
    }
}

/// Gathers bits selected by a mask into a dense bitmap.
///
/// # Safety
///
/// All bitmaps must be valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_gather(
    dst: *mut Word,
    src: *const Word,
    mask: *const Word,
    bits: u32,
) {
    let bits = bits as usize;
    // SAFETY: The destination is valid for `bits` bits.
    unsafe { zero(dst, bits) };

    let mut destination_bit = 0;
    let mut mask_bit = 0;
    loop {
        // SAFETY: The mask bitmap is valid for `bits` bits.
        mask_bit = unsafe { find_bit::find_next_set(mask, bits, mask_bit) };
        if mask_bit >= bits {
            break;
        }

        // SAFETY: `mask_bit < bits`.
        if unsafe { test_bit(src, mask_bit) } {
            // SAFETY: The number of selected bits is bounded by `bits`.
            unsafe { set_bit(dst, destination_bit) };
        }
        destination_bit += 1;
        mask_bit += 1;
    }
}

/// Copies a u32 array into a 64-bit bitmap.
///
/// # Safety
///
/// `bitmap` must be writable for `bits` bits and `buf` must contain
/// `ceil(bits / 32)` elements.
#[export]
unsafe extern "C" fn rust_bitmap_from_arr32(bitmap: *mut Word, buf: *const u32, bits: u32) {
    let bits = bits as usize;
    let halfwords = bits.div_ceil(32);
    let mut index = 0;

    while index < halfwords {
        // SAFETY: The caller guarantees the input array length.
        let low = unsafe { ptr::read(buf.add(index)) } as Word;
        let high = if index + 1 < halfwords {
            // SAFETY: The branch condition keeps the read inside the input array.
            (unsafe { ptr::read(buf.add(index + 1)) } as Word) << 32
        } else {
            0
        };
        // SAFETY: Each pair consumes at most one 64-bit bitmap word.
        unsafe { write_word(bitmap, index / 2, low | high) };
        index += 2;
    }

    if bits % BITS != 0 {
        let index = (halfwords - 1) / 2;
        // SAFETY: A non-zero tail implies this final word exists.
        let value = unsafe { read_word(bitmap.cast_const(), index) } & last_word_mask(bits);
        // SAFETY: A non-zero tail implies this final word exists.
        unsafe { write_word(bitmap, index, value) };
    }
}

/// Copies a 64-bit bitmap into a u32 array.
///
/// # Safety
///
/// `buf` must be writable for `ceil(bits / 32)` elements and `bitmap` must be
/// valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_to_arr32(buf: *mut u32, bitmap: *const Word, bits: u32) {
    let bits = bits as usize;
    let halfwords = bits.div_ceil(32);

    for index in 0..halfwords {
        // SAFETY: Each pair reads at most one 64-bit bitmap word.
        let value = unsafe { read_word(bitmap, index / 2) };
        let halfword = if index & 1 == 0 {
            value as u32
        } else {
            (value >> 32) as u32
        };
        // SAFETY: The caller guarantees the output array length.
        unsafe { ptr::write(buf.add(index), halfword) };
    }

    if bits % BITS != 0 {
        let tail = bits % 32;
        let mask = if tail == 0 {
            u32::MAX
        } else {
            (1u32 << tail) - 1
        };
        // SAFETY: A non-zero tail implies this final array element exists.
        let value = unsafe { ptr::read(buf.add(halfwords - 1)) } & mask;
        // SAFETY: A non-zero tail implies this final array element exists.
        unsafe { ptr::write(buf.add(halfwords - 1), value) };
    }
}

/// Copies a u64 array into a 64-bit bitmap.
///
/// # Safety
///
/// `bitmap` must be writable for `bits` bits and `buf` must contain
/// `ceil(bits / 64)` elements.
#[export]
unsafe extern "C" fn rust_bitmap_from_arr64(bitmap: *mut Word, buf: *const u64, bits: u32) {
    let bits = bits as usize;
    let count = words(bits);

    for index in 0..count {
        // SAFETY: The caller guarantees the input array length.
        let value = unsafe { ptr::read(buf.add(index)) } as Word;
        // SAFETY: The loop bound keeps the write inside the bitmap.
        unsafe { write_word(bitmap, index, value) };
    }

    if bits % BITS != 0 {
        // SAFETY: A non-zero tail implies this final word exists.
        let value = unsafe { read_word(bitmap.cast_const(), count - 1) } & last_word_mask(bits);
        // SAFETY: A non-zero tail implies this final word exists.
        unsafe { write_word(bitmap, count - 1, value) };
    }
}

/// Copies a 64-bit bitmap into a u64 array.
///
/// # Safety
///
/// `buf` must be writable for `ceil(bits / 64)` elements and `bitmap` must be
/// valid for `bits` bits.
#[export]
unsafe extern "C" fn rust_bitmap_to_arr64(buf: *mut u64, bitmap: *const Word, bits: u32) {
    let bits = bits as usize;
    let count = words(bits);

    for index in 0..count {
        // SAFETY: The loop bound keeps the read inside the bitmap.
        let value = unsafe { read_word(bitmap, index) } as u64;
        // SAFETY: The caller guarantees the output array length.
        unsafe { ptr::write(buf.add(index), value) };
    }

    if bits % 64 != 0 {
        let tail = bits % 64;
        let mask = if tail == 0 {
            u64::MAX
        } else {
            (1u64 << tail) - 1
        };
        // SAFETY: A non-zero tail implies this final array element exists.
        let value = unsafe { ptr::read(buf.add(count - 1)) } & mask;
        // SAFETY: A non-zero tail implies this final array element exists.
        unsafe { ptr::write(buf.add(count - 1), value) };
    }
}
