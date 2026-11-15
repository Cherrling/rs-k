// SPDX-License-Identifier: GPL-2.0-only

//! Native implementation of the generic kernel FIFO algorithms.
//!
//! Rust owns ring-buffer copying, wrap-around calculations, record framing,
//! linear range selection, user-copy orchestration, DMA segment preparation,
//! and index advancement. C retains allocation and the low-level `uaccess`,
//! memory-barrier, and scatterlist mutation boundaries.

use crate::bindings;
use core::cmp;
use core::ffi::{c_int, c_uint, c_void};
use core::ptr;
use macros::export;

type Fifo = bindings::__kfifo;
type Scatterlist = bindings::scatterlist;

const DMA_MAPPING_ERROR: bindings::dma_addr_t = bindings::dma_addr_t::MAX;

#[derive(Clone, Copy)]
struct Ring {
    in_index: c_uint,
    out_index: c_uint,
    mask: c_uint,
    esize: c_uint,
    data: *mut u8,
}

impl Ring {
    /// Loads the shared FIFO representation.
    ///
    /// # Safety
    ///
    /// `fifo` must point to a valid `struct __kfifo`.
    unsafe fn from_fifo(fifo: *mut Fifo) -> Self {
        // SAFETY: Guaranteed by this function's contract.
        unsafe {
            Self {
                in_index: (*fifo).in_,
                out_index: (*fifo).out,
                mask: (*fifo).mask,
                esize: (*fifo).esize,
                data: (*fifo).data.cast(),
            }
        }
    }

    #[inline(always)]
    fn capacity(self) -> c_uint {
        self.mask.wrapping_add(1)
    }

    #[inline(always)]
    fn available(self) -> c_uint {
        self.in_index.wrapping_sub(self.out_index)
    }

    #[inline(always)]
    fn unused(self) -> c_uint {
        self.capacity().wrapping_sub(self.available())
    }

    /// Stores the new input index in the shared representation.
    ///
    /// # Safety
    ///
    /// `fifo` must remain valid for the duration of the operation.
    unsafe fn commit_in(self, fifo: *mut Fifo, index: c_uint) {
        // SAFETY: Guaranteed by this function's contract.
        unsafe { (*fifo).in_ = index };
    }

    /// Stores the new output index in the shared representation.
    ///
    /// # Safety
    ///
    /// `fifo` must remain valid for the duration of the operation.
    unsafe fn commit_out(self, fifo: *mut Fifo, index: c_uint) {
        // SAFETY: Guaranteed by this function's contract.
        unsafe { (*fifo).out = index };
    }

    #[inline(always)]
    fn byte_ranges(self, len: c_uint, off: c_uint) -> (c_uint, c_uint, c_uint) {
        let mut len = len;
        let mut off = off & self.mask;
        let mut size = self.capacity();

        if self.esize != 1 {
            off = off.wrapping_mul(self.esize);
            size = size.wrapping_mul(self.esize);
            len = len.wrapping_mul(self.esize);
        }

        (len, off, size)
    }

    #[inline(always)]
    fn first_range(len: c_uint, off: c_uint, size: c_uint) -> c_uint {
        cmp::min(len, size.wrapping_sub(off))
    }

    /// Copies an element range into the ring buffer.
    ///
    /// # Safety
    ///
    /// The FIFO must be initialized, and `src` must contain `len` elements.
    unsafe fn copy_in(self, src: *const c_void, len: c_uint, off: c_uint) {
        let (len, off, size) = self.byte_ranges(len, off);

        if len != 0 {
            let first = Self::first_range(len, off, size);
            let second = len - first;
            let dst = self.data;
            let src = src.cast::<u8>();

            // SAFETY: The caller guarantees the source range. The initialized
            // ring storage is large enough for the wrapped destination ranges.
            unsafe {
                ptr::copy_nonoverlapping(src, dst.add(off as usize), first as usize);
                if second != 0 {
                    ptr::copy_nonoverlapping(src.add(first as usize), dst, second as usize);
                }
            }
        }

        // Make the data visible before the caller publishes the input index.
        // SAFETY: Calling the C memory-barrier helper has no additional
        // preconditions.
        unsafe { bindings::smp_wmb() };
    }

    /// Copies an element range from the ring buffer.
    ///
    /// # Safety
    ///
    /// The FIFO must be initialized, and `dst` must hold `len` elements.
    unsafe fn copy_out(self, dst: *mut c_void, len: c_uint, off: c_uint) {
        let (len, off, size) = self.byte_ranges(len, off);

        if len != 0 {
            let first = Self::first_range(len, off, size);
            let second = len - first;
            let src = self.data;
            let dst = dst.cast::<u8>();

            // SAFETY: The caller guarantees the destination range. The
            // initialized ring storage is valid for both wrapped source ranges.
            unsafe {
                ptr::copy_nonoverlapping(src.add(off as usize), dst, first as usize);
                if second != 0 {
                    ptr::copy_nonoverlapping(src, dst.add(first as usize), second as usize);
                }
            }
        }

        // Make the copy visible before the caller publishes the output index.
        // SAFETY: Calling the C memory-barrier helper has no additional
        // preconditions.
        unsafe { bindings::smp_wmb() };
    }

    #[inline(always)]
    fn div_round_up(value: usize, divisor: c_uint) -> usize {
        value
            .wrapping_add(divisor as usize - 1)
            .wrapping_div(divisor as usize)
    }

    /// Copies user data into the ring buffer.
    ///
    /// Returns the number of whole elements that were not copied.
    ///
    /// # Safety
    ///
    /// The FIFO must be initialized. `from` must be a user-space pointer,
    /// `copied` must be writable, and both ranges must be valid.
    unsafe fn copy_from_user(
        self,
        from: *const c_void,
        len: c_uint,
        off: c_uint,
        copied: *mut c_uint,
    ) -> usize {
        let (len, off, size) = self.byte_ranges(len, off);
        let first = Self::first_range(len, off, size);
        let second = len - first;
        let dst = self.data;
        let from = from.cast::<u8>();
        let mut ret;

        // SAFETY: The caller guarantees the source range and the initialized
        // ring storage. C owns the architecture-specific user-copy primitive.
        unsafe {
            ret =
                bindings::copy_from_user(dst.add(off as usize).cast(), from.cast(), first as usize);
        }

        if ret != 0 {
            ret = Self::div_round_up(ret.wrapping_add((len - first) as usize), self.esize);
        } else {
            // SAFETY: The first copy succeeded. The source and wrapped
            // destination ranges are valid for `second` bytes.
            unsafe {
                ret = bindings::copy_from_user(
                    dst.cast(),
                    from.add(first as usize).cast(),
                    second as usize,
                );
            }
            if ret != 0 {
                ret = Self::div_round_up(ret, self.esize);
            }
        }

        // Make copied data visible before the caller publishes the input index.
        // SAFETY: Calling the C memory-barrier helper has no additional
        // preconditions.
        unsafe { bindings::smp_wmb() };

        // SAFETY: The caller guarantees that `copied` is writable.
        unsafe {
            *copied = len.wrapping_sub((ret as c_uint).wrapping_mul(self.esize));
        }
        ret
    }

    /// Copies ring-buffer data to user space.
    ///
    /// Returns the number of whole elements that were not copied.
    ///
    /// # Safety
    ///
    /// The FIFO must be initialized. `to` must be a user-space pointer,
    /// `copied` must be writable, and both ranges must be valid.
    unsafe fn copy_to_user(
        self,
        to: *mut c_void,
        len: c_uint,
        off: c_uint,
        copied: *mut c_uint,
    ) -> usize {
        let (len, off, size) = self.byte_ranges(len, off);
        let first = Self::first_range(len, off, size);
        let second = len - first;
        let src = self.data;
        let to = to.cast::<u8>();
        let mut ret;

        // SAFETY: The caller guarantees the destination range and the
        // initialized ring storage. C owns the user-copy primitive.
        unsafe {
            ret = bindings::copy_to_user(to.cast(), src.add(off as usize).cast(), first as usize);
        }

        if ret != 0 {
            ret = Self::div_round_up(ret.wrapping_add((len - first) as usize), self.esize);
        } else {
            // SAFETY: The first copy succeeded. The wrapped source and user
            // destination ranges are valid for `second` bytes.
            unsafe {
                ret = bindings::copy_to_user(
                    to.add(first as usize).cast(),
                    src.cast(),
                    second as usize,
                );
            }
            if ret != 0 {
                ret = Self::div_round_up(ret, self.esize);
            }
        }

        // Make the copy visible before the caller publishes the output index.
        // SAFETY: Calling the C memory-barrier helper has no additional
        // preconditions.
        unsafe { bindings::smp_wmb() };

        // SAFETY: The caller guarantees that `copied` is writable.
        unsafe {
            *copied = len.wrapping_sub((ret as c_uint).wrapping_mul(self.esize));
        }
        ret
    }

    /// Initializes one scatterlist segment.
    ///
    /// # Safety
    ///
    /// `sgl` must point to writable scatterlist storage. The FIFO data range
    /// must be valid.
    unsafe fn setup_sgl_buf(
        self,
        sgl: *mut Scatterlist,
        data_offset: c_uint,
        nents: c_int,
        len: c_uint,
        dma: bindings::dma_addr_t,
    ) -> c_uint {
        if nents == 0 || len == 0 {
            return 0;
        }

        let set_dma = dma != DMA_MAPPING_ERROR;
        let dma_address = dma.wrapping_add(bindings::dma_addr_t::from(data_offset));

        // SAFETY: The caller guarantees writable scatterlist storage and a
        // valid FIFO data range. C owns the inline scatterlist mutations.
        unsafe {
            bindings::kfifo_set_sg(
                sgl,
                self.data.add(data_offset as usize).cast(),
                len,
                dma_address,
                set_dma,
            );
        }
        1
    }

    /// Splits one FIFO range into up to two scatterlist segments.
    ///
    /// # Safety
    ///
    /// `sgl` must have space for the returned number of entries. The FIFO data
    /// range must be valid.
    unsafe fn setup_sgl(
        self,
        sgl: *mut Scatterlist,
        nents: c_int,
        len: c_uint,
        off: c_uint,
        dma: bindings::dma_addr_t,
    ) -> c_uint {
        let (len, off, size) = self.byte_ranges(len, off);
        let len_to_end = cmp::min(len, size.wrapping_sub(off));
        // SAFETY: The caller guarantees scatterlist storage for at least
        // `nents` entries and a valid FIFO data range.
        let n = unsafe { self.setup_sgl_buf(sgl, off, nents, len_to_end, dma) };
        // SAFETY: `n` is at most one and at most `nents`. The resulting pointer
        // is therefore inside the scatterlist or one past its final entry.
        let next = unsafe { sgl.add(n as usize) };

        // SAFETY: The first range consumed at most one entry. The remaining
        // entry count and the wrapped FIFO range are valid under the caller's
        // contract.
        n.wrapping_add(unsafe {
            self.setup_sgl_buf(
                next,
                0,
                nents.wrapping_sub(n as c_int),
                len.wrapping_sub(len_to_end),
                dma,
            )
        })
    }

    /// Reads the length prefix of the next record.
    ///
    /// # Safety
    ///
    /// The FIFO must contain a record, and `recsize` must be one or two.
    unsafe fn peek_n(self, recsize: usize) -> c_uint {
        let data = self.data;
        let out = self.out_index;
        // SAFETY: The record guarantees that its first byte is in the ring.
        let mut len = c_uint::from(unsafe { ptr::read(data.add((out & self.mask) as usize)) });

        if recsize > 1 {
            // SAFETY: A multi-byte record prefix has a second byte in the ring.
            let high = unsafe { ptr::read(data.add(((out.wrapping_add(1)) & self.mask) as usize)) };
            len |= c_uint::from(high) << 8;
        }

        len
    }

    /// Writes the length prefix of a record.
    ///
    /// # Safety
    ///
    /// There must be space for the record prefix, and `recsize` must be one or
    /// two.
    unsafe fn poke_n(self, len: c_uint, recsize: usize) {
        let data = self.data;
        let input = self.in_index;

        // SAFETY: The caller guarantees space for the first prefix byte.
        unsafe {
            ptr::write(data.add((input & self.mask) as usize), len as u8);
        }

        if recsize > 1 {
            // SAFETY: The caller guarantees space for the second prefix byte.
            unsafe {
                ptr::write(
                    data.add(((input.wrapping_add(1)) & self.mask) as usize),
                    (len >> 8) as u8,
                );
            }
        }
    }
}

#[inline(always)]
fn record_max(recsize: usize) -> c_uint {
    let bits = recsize.wrapping_mul(8);

    if bits >= c_uint::BITS as usize {
        c_uint::MAX
    } else {
        (1u32 << bits) - 1
    }
}

/// Copies elements into a FIFO.
///
/// # Safety
///
/// `fifo` must be initialized, and `buf` must contain `len` elements.
#[export]
unsafe extern "C" fn rust_kfifo_in(fifo: *mut Fifo, buf: *const c_void, len: c_uint) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let len = cmp::min(len, ring.unused());

    // SAFETY: The source contains the clamped element count. The ring has
    // enough unused element slots for that count.
    unsafe { ring.copy_in(buf, len, ring.in_index) };
    // SAFETY: The FIFO remains initialized while this operation runs.
    unsafe { ring.commit_in(fifo, ring.in_index.wrapping_add(len)) };
    len
}

/// Copies elements from a FIFO without consuming them.
///
/// # Safety
///
/// `fifo` must be initialized, and `buf` must hold `len` elements.
#[export]
unsafe extern "C" fn rust_kfifo_out_peek(fifo: *mut Fifo, buf: *mut c_void, len: c_uint) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let len = cmp::min(len, ring.available());

    // SAFETY: The destination holds at least the clamped element count. The
    // ring contains that many available elements.
    unsafe { ring.copy_out(buf, len, ring.out_index) };
    len
}

/// Returns the linear output range and its element offset.
///
/// # Safety
///
/// `fifo` must be initialized. If non-NULL, `tail` must be writable.
#[export]
unsafe extern "C" fn rust_kfifo_out_linear(
    fifo: *mut Fifo,
    tail: *mut c_uint,
    n: c_uint,
) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let off = ring.out_index & ring.mask;

    if !tail.is_null() {
        // SAFETY: The caller guarantees that `tail` is writable.
        unsafe { *tail = off };
    }

    cmp::min(
        cmp::min(n, ring.available()),
        ring.capacity().wrapping_sub(off),
    )
}

/// Copies and consumes elements from a FIFO.
///
/// # Safety
///
/// `fifo` must be initialized, and `buf` must hold `len` elements.
#[export]
unsafe extern "C" fn rust_kfifo_out(fifo: *mut Fifo, buf: *mut c_void, len: c_uint) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let len = cmp::min(len, ring.available());

    // SAFETY: The destination holds at least the clamped element count. The
    // ring contains that many available elements.
    unsafe { ring.copy_out(buf, len, ring.out_index) };
    // SAFETY: The FIFO remains initialized while this operation runs.
    unsafe { ring.commit_out(fifo, ring.out_index.wrapping_add(len)) };
    len
}

/// Copies user data into a FIFO.
///
/// # Safety
///
/// All pointers and user ranges must be valid for the requested operation.
#[export]
unsafe extern "C" fn rust_kfifo_from_user(
    fifo: *mut Fifo,
    from: *const c_void,
    len: usize,
    copied: *mut c_uint,
) -> c_int {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let mut len = if ring.esize == 1 {
        len
    } else {
        len / ring.esize as usize
    };
    let available = ring.unused() as usize;

    if len > available {
        len = available;
    }

    // SAFETY: The user pointer, destination ring range, and `copied` pointer
    // satisfy the helper contract. The FIFO bounds limit `len`.
    let ret = unsafe { ring.copy_from_user(from, len as c_uint, ring.in_index, copied) };
    let (committed, err) = if ret != 0 {
        (len.wrapping_sub(ret), -(bindings::EFAULT as c_int))
    } else {
        (len, 0)
    };

    // SAFETY: The FIFO remains initialized. Only successfully copied elements
    // are added to the input index.
    unsafe { ring.commit_in(fifo, ring.in_index.wrapping_add(committed as c_uint)) };
    err
}

/// Copies FIFO data to user space.
///
/// # Safety
///
/// All pointers and user ranges must be valid for the requested operation.
#[export]
unsafe extern "C" fn rust_kfifo_to_user(
    fifo: *mut Fifo,
    to: *mut c_void,
    len: usize,
    copied: *mut c_uint,
) -> c_int {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let mut len = if ring.esize == 1 {
        len
    } else {
        len / ring.esize as usize
    };
    let available = ring.available() as usize;

    if len > available {
        len = available;
    }

    // SAFETY: The user pointer, source ring range, and `copied` pointer satisfy
    // the helper contract. The FIFO bounds limit `len`.
    let ret = unsafe { ring.copy_to_user(to, len as c_uint, ring.out_index, copied) };
    let (committed, err) = if ret != 0 {
        (len.wrapping_sub(ret), -(bindings::EFAULT as c_int))
    } else {
        (len, 0)
    };

    // SAFETY: The FIFO remains initialized. Only successfully copied elements
    // are added to the output index.
    unsafe { ring.commit_out(fifo, ring.out_index.wrapping_add(committed as c_uint)) };
    err
}

/// Prepares a scatterlist for DMA input.
///
/// # Safety
///
/// `fifo` and `sgl` must be valid, and `sgl` must have enough entries.
#[export]
unsafe extern "C" fn rust_kfifo_dma_in_prepare(
    fifo: *mut Fifo,
    sgl: *mut Scatterlist,
    nents: c_int,
    len: c_uint,
    dma: bindings::dma_addr_t,
) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let len = cmp::min(len, ring.unused());

    // SAFETY: The scatterlist has enough entries for the clamped input range.
    // The FIFO remains initialized for the operation.
    unsafe { ring.setup_sgl(sgl, nents, len, ring.in_index, dma) }
}

/// Prepares a scatterlist for DMA output.
///
/// # Safety
///
/// `fifo` and `sgl` must be valid, and `sgl` must have enough entries.
#[export]
unsafe extern "C" fn rust_kfifo_dma_out_prepare(
    fifo: *mut Fifo,
    sgl: *mut Scatterlist,
    nents: c_int,
    len: c_uint,
    dma: bindings::dma_addr_t,
) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let len = cmp::min(len, ring.available());

    // SAFETY: The scatterlist has enough entries for the clamped output range.
    // The FIFO remains initialized for the operation.
    unsafe { ring.setup_sgl(sgl, nents, len, ring.out_index, dma) }
}

/// Limits a record length to the range representable by its prefix.
///
/// # Safety
///
/// The C ABI has no additional pointer preconditions.
#[export]
unsafe extern "C" fn rust_kfifo_max_r(len: c_uint, recsize: usize) -> c_uint {
    cmp::min(len, record_max(recsize))
}

/// Reads the next record length.
///
/// # Safety
///
/// `fifo` must contain a record, and `recsize` must be one or two.
#[export]
unsafe extern "C" fn rust_kfifo_len_r(fifo: *mut Fifo, recsize: usize) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    // SAFETY: The function contract requires a complete next record and a
    // supported prefix size.
    unsafe { ring.peek_n(recsize) }
}

/// Appends one record to a FIFO.
///
/// # Safety
///
/// `fifo` must be initialized, and `buf` must contain `len` bytes.
#[export]
unsafe extern "C" fn rust_kfifo_in_r(
    fifo: *mut Fifo,
    buf: *const c_void,
    len: c_uint,
    recsize: usize,
) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };

    if len as usize + recsize > ring.unused() as usize {
        return 0;
    }

    // SAFETY: The size check reserved space for the complete prefix and data
    // record. The source contains the requested data, and the FIFO remains
    // initialized.
    unsafe {
        ring.poke_n(len, recsize);
        ring.copy_in(buf, len, ring.in_index.wrapping_add(recsize as c_uint));
        ring.commit_in(
            fifo,
            ring.in_index
                .wrapping_add(len)
                .wrapping_add(recsize as c_uint),
        );
    }
    len
}

/// Copies bytes from the next record without consuming it.
///
/// # Safety
///
/// `fifo` must be initialized, and `buf` must hold `len` bytes.
#[export]
unsafe extern "C" fn rust_kfifo_out_peek_r(
    fifo: *mut Fifo,
    buf: *mut c_void,
    len: c_uint,
    recsize: usize,
) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };

    if ring.in_index == ring.out_index {
        return 0;
    }

    // SAFETY: The non-empty FIFO contains a complete record with a supported
    // prefix size.
    let record_len = unsafe { ring.peek_n(recsize) };
    let len = cmp::min(len, record_len);
    // SAFETY: The destination holds `len` bytes, and the record contains
    // `record_len` bytes after its prefix.
    unsafe { ring.copy_out(buf, len, ring.out_index.wrapping_add(recsize as c_uint)) };
    len
}

/// Returns the linear range of the next record.
///
/// # Safety
///
/// `fifo` must be initialized. If non-NULL, `tail` must be writable.
#[export]
unsafe extern "C" fn rust_kfifo_out_linear_r(
    fifo: *mut Fifo,
    tail: *mut c_uint,
    n: c_uint,
    recsize: usize,
) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };

    if ring.in_index == ring.out_index {
        return 0;
    }

    if !tail.is_null() {
        // SAFETY: The caller guarantees that `tail` is writable.
        unsafe { *tail = ring.out_index.wrapping_add(recsize as c_uint) };
    }

    // SAFETY: The non-empty FIFO contains a complete record with a supported
    // prefix size.
    let record_len = unsafe { ring.peek_n(recsize) };
    cmp::min(n, record_len)
}

/// Copies and consumes bytes from the next record.
///
/// # Safety
///
/// `fifo` must be initialized, and `buf` must hold `len` bytes.
#[export]
unsafe extern "C" fn rust_kfifo_out_r(
    fifo: *mut Fifo,
    buf: *mut c_void,
    len: c_uint,
    recsize: usize,
) -> c_uint {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };

    if ring.in_index == ring.out_index {
        return 0;
    }

    // SAFETY: The non-empty FIFO contains a complete record with a supported
    // prefix size.
    let record_len = unsafe { ring.peek_n(recsize) };
    let len = cmp::min(len, record_len);
    // SAFETY: The destination holds `len` bytes, the record contains
    // `record_len` bytes, and the FIFO remains initialized.
    unsafe {
        ring.copy_out(buf, len, ring.out_index.wrapping_add(recsize as c_uint));
        ring.commit_out(
            fifo,
            ring.out_index
                .wrapping_add(record_len)
                .wrapping_add(recsize as c_uint),
        );
    }
    len
}

/// Skips the next record.
///
/// # Safety
///
/// `fifo` must contain a record, and `recsize` must be one or two.
#[export]
unsafe extern "C" fn rust_kfifo_skip_r(fifo: *mut Fifo, recsize: usize) {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    // SAFETY: The function contract requires a complete next record and a
    // supported prefix size.
    let len = unsafe { ring.peek_n(recsize) };

    // SAFETY: The complete record length and prefix advance the output index
    // to the start of the following record. The FIFO remains initialized.
    unsafe {
        ring.commit_out(
            fifo,
            ring.out_index
                .wrapping_add(len)
                .wrapping_add(recsize as c_uint),
        )
    };
}

/// Copies one record from user space into a FIFO.
///
/// # Safety
///
/// All pointers and user ranges must be valid for the requested operation.
#[export]
unsafe extern "C" fn rust_kfifo_from_user_r(
    fifo: *mut Fifo,
    from: *const c_void,
    len: usize,
    copied: *mut c_uint,
    recsize: usize,
) -> c_int {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let max = cmp::min(len as c_uint, record_max(recsize));
    let len = cmp::min(len, max as usize);

    if len as usize + recsize > ring.unused() as usize {
        // SAFETY: The caller guarantees that `copied` is writable.
        unsafe { *copied = 0 };
        return 0;
    }

    // SAFETY: The size check reserved the prefix and payload. The prefix size
    // is supported by the public record API.
    unsafe { ring.poke_n(len as c_uint, recsize) };
    // SAFETY: The user pointer, destination payload range, and `copied`
    // pointer satisfy the helper contract.
    let ret = unsafe {
        ring.copy_from_user(
            from,
            len as c_uint,
            ring.in_index.wrapping_add(recsize as c_uint),
            copied,
        )
    };

    if ret != 0 {
        // SAFETY: The caller guarantees that `copied` is writable.
        unsafe { *copied = 0 };
        return -(bindings::EFAULT as c_int);
    }

    // SAFETY: The complete record copy succeeded and the FIFO remains
    // initialized. The committed length includes the prefix and payload.
    unsafe {
        ring.commit_in(
            fifo,
            ring.in_index
                .wrapping_add(len as c_uint)
                .wrapping_add(recsize as c_uint),
        )
    };
    0
}

/// Copies one record from a FIFO to user space.
///
/// # Safety
///
/// All pointers and user ranges must be valid for the requested operation.
#[export]
unsafe extern "C" fn rust_kfifo_to_user_r(
    fifo: *mut Fifo,
    to: *mut c_void,
    len: usize,
    copied: *mut c_uint,
    recsize: usize,
) -> c_int {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };

    if ring.in_index == ring.out_index {
        // SAFETY: The caller guarantees that `copied` is writable.
        unsafe { *copied = 0 };
        return 0;
    }

    // SAFETY: The non-empty FIFO contains a complete record with a supported
    // prefix size.
    let record_len = unsafe { ring.peek_n(recsize) };
    let len = cmp::min(len, record_len as usize) as c_uint;
    // SAFETY: The user pointer, source record range, and `copied` pointer
    // satisfy the helper contract.
    let ret = unsafe {
        ring.copy_to_user(
            to,
            len,
            ring.out_index.wrapping_add(recsize as c_uint),
            copied,
        )
    };

    if ret != 0 {
        // SAFETY: The caller guarantees that `copied` is writable.
        unsafe { *copied = 0 };
        return -(bindings::EFAULT as c_int);
    }

    // SAFETY: The complete record copy succeeded and the FIFO remains
    // initialized. The committed length consumes the prefix and payload.
    unsafe {
        ring.commit_out(
            fifo,
            ring.out_index
                .wrapping_add(record_len)
                .wrapping_add(recsize as c_uint),
        )
    };
    0
}

/// Prepares a scatterlist for one DMA input record.
///
/// # Safety
///
/// `fifo` and `sgl` must be valid, `nents` must be non-zero, and `sgl` must
/// have enough entries.
#[export]
unsafe extern "C" fn rust_kfifo_dma_in_prepare_r(
    fifo: *mut Fifo,
    sgl: *mut Scatterlist,
    nents: c_int,
    len: c_uint,
    recsize: usize,
    dma: bindings::dma_addr_t,
) -> c_uint {
    if nents == 0 {
        // SAFETY: This preserves the baseline BUG_ON(!nents) precondition.
        unsafe { bindings::BUG() };
    }

    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let len = cmp::min(len, record_max(recsize));

    if len as usize + recsize > ring.unused() as usize {
        return 0;
    }

    // SAFETY: The record fits in the free space. The scatterlist has enough
    // entries, and its payload range remains valid for the FIFO lifetime.
    unsafe {
        ring.setup_sgl(
            sgl,
            nents,
            len,
            ring.in_index.wrapping_add(recsize as c_uint),
            dma,
        )
    }
}

/// Publishes one completed DMA input record.
///
/// # Safety
///
/// `fifo` must be initialized and have space for the completed record.
#[export]
unsafe extern "C" fn rust_kfifo_dma_in_finish_r(fifo: *mut Fifo, len: c_uint, recsize: usize) {
    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let len = cmp::min(len, record_max(recsize));

    // SAFETY: The caller guarantees space for the complete DMA record. Writing
    // the prefix and committing its length publishes that reserved record.
    unsafe {
        ring.poke_n(len, recsize);
        ring.commit_in(
            fifo,
            ring.in_index
                .wrapping_add(len)
                .wrapping_add(recsize as c_uint),
        );
    }
}

/// Prepares a scatterlist for one DMA output record.
///
/// # Safety
///
/// `fifo` and `sgl` must be valid, `nents` must be non-zero, and `sgl` must
/// have enough entries.
#[export]
unsafe extern "C" fn rust_kfifo_dma_out_prepare_r(
    fifo: *mut Fifo,
    sgl: *mut Scatterlist,
    nents: c_int,
    len: c_uint,
    recsize: usize,
    dma: bindings::dma_addr_t,
) -> c_uint {
    if nents == 0 {
        // SAFETY: This preserves the baseline BUG_ON(!nents) precondition.
        unsafe { bindings::BUG() };
    }

    // SAFETY: The function contract requires an initialized FIFO.
    let ring = unsafe { Ring::from_fifo(fifo) };
    let len = cmp::min(len, record_max(recsize));

    if len as usize + recsize > ring.available() as usize {
        return 0;
    }

    // SAFETY: The record fits in the available data. The scatterlist has
    // enough entries, and its payload range remains valid for the operation.
    unsafe {
        ring.setup_sgl(
            sgl,
            nents,
            len,
            ring.out_index.wrapping_add(recsize as c_uint),
            dma,
        )
    }
}
