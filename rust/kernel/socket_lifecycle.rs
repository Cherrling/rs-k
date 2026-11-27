// SPDX-License-Identifier: GPL-2.0

//! Native socket and sock lifecycle.
//!
//! Rust owns socket inode allocation and file binding, protocol family
//! validation and callback ordering, socket close rollback, sock allocation
//! rollback, reference transitions, and the final sock destruction order.
//!
//! C retains the shared layouts, inode and file allocation primitives,
//! security hooks, net namespace and socket accounting, module references,
//! memcg and cgroup accounting, locks, RCU, and protocol callbacks.

use crate::bindings;
use core::ffi::{c_int, c_uint, c_void};
use macros::export;

type CallbackHead = bindings::callback_head;
type File = bindings::file;
type Gfp = bindings::gfp_t;
type Inode = bindings::inode;
type Net = bindings::net;
type Proto = bindings::proto;
type Sock = bindings::sock;
type Socket = bindings::socket;

const PF_INET: c_int = bindings::PF_INET as c_int;
const PF_PACKET: c_int = bindings::PF_PACKET as c_int;
const SOCK_PACKET: c_int = bindings::sock_type_SOCK_PACKET as c_int;
const SOCK_MAX: c_int = SOCK_PACKET + 1;
const NPROTO: c_int = bindings::NPROTO as c_int;
const SOCK_TYPE_MASK: c_int = bindings::SOCK_TYPE_MASK as c_int;
const SOCK_CLOEXEC: c_int = bindings::SOCK_CLOEXEC as c_int;
const SOCK_NONBLOCK: c_int = bindings::SOCK_NONBLOCK as c_int;
const O_RDWR: c_int = bindings::O_RDWR as c_int;
const O_NONBLOCK: c_int = bindings::O_NONBLOCK as c_int;
const O_CLOEXEC: c_int = bindings::O_CLOEXEC as c_int;

crate::static_assert!(SOCK_CLOEXEC == O_CLOEXEC);
crate::static_assert!((SOCK_MAX | SOCK_TYPE_MASK) == SOCK_TYPE_MASK);
crate::static_assert!((SOCK_CLOEXEC & SOCK_TYPE_MASK) == 0);
crate::static_assert!((SOCK_NONBLOCK & SOCK_TYPE_MASK) == 0);

const COUNTER_RELEASE: c_uint = bindings::RUST_SOCKET_TEST_COUNTER_RELEASE as c_uint;
const COUNTER_SK_ALLOC: c_uint = bindings::RUST_SOCKET_TEST_COUNTER_SK_ALLOC as c_uint;
const COUNTER_SK_FREE: c_uint = bindings::RUST_SOCKET_TEST_COUNTER_SK_FREE as c_uint;
const COUNTER_RCU_FREE: c_uint = bindings::RUST_SOCKET_TEST_COUNTER_RCU_FREE as c_uint;

const FAIL_FAMILY: c_uint = bindings::RUST_SOCKET_TEST_FAIL_FAMILY as c_uint;
const FAIL_FAMILY_MODULE: c_uint = bindings::RUST_SOCKET_TEST_FAIL_FAMILY_MODULE as c_uint;
const FAIL_CREATE: c_uint = bindings::RUST_SOCKET_TEST_FAIL_CREATE as c_uint;
const FAIL_OPS_MODULE: c_uint = bindings::RUST_SOCKET_TEST_FAIL_OPS_MODULE as c_uint;
const FAIL_SECURITY_POST: c_uint = bindings::RUST_SOCKET_TEST_FAIL_SECURITY_POST as c_uint;

fn negative(error: u32) -> c_int {
    -(error as c_int)
}

fn is_err(pointer: *const c_void) -> bool {
    // SAFETY: The helper only classifies the pointer value.
    unsafe { bindings::rust_helper_file_is_err(pointer) }
}

fn err_ptr(error: isize) -> *mut c_void {
    // SAFETY: The helper only encodes the error value in a pointer.
    unsafe { bindings::rust_helper_file_err_ptr(error) }
}

#[cfg(CONFIG_SOCKET_LIFECYCLE_KUNIT_TEST)]
fn injected(stage: c_uint) -> bool {
    // SAFETY: The helper reads the test-only one-shot failure state.
    unsafe { bindings::rust_helper_socket_test_fail(stage) }
}

#[cfg(not(CONFIG_SOCKET_LIFECYCLE_KUNIT_TEST))]
#[inline]
fn injected(_stage: c_uint) -> bool {
    false
}

#[cfg(CONFIG_SOCKET_LIFECYCLE_KUNIT_TEST)]
fn record(counter: c_uint) {
    // SAFETY: The helper updates the test-only observation counters.
    unsafe { bindings::rust_helper_socket_test_record(counter) }
}

#[cfg(not(CONFIG_SOCKET_LIFECYCLE_KUNIT_TEST))]
#[inline]
fn record(_counter: c_uint) {}

/// Releases a socket and its inode or file binding.
///
/// # Safety
///
/// `sock` must be a live socket. `inode` must be its socket inode when it is
/// non-null.
unsafe fn socket_release_inner(sock: *mut Socket, inode: *mut Inode) {
    // SAFETY: The caller supplies a live socket.
    let ops = unsafe { bindings::rust_helper_socket_ops(sock) };

    if !ops.is_null() {
        // SAFETY: A live operation table keeps its owner and callbacks valid.
        let owner = unsafe { (*ops).owner };
        if !inode.is_null() {
            // SAFETY: The caller supplied the live socket inode.
            unsafe { bindings::rust_helper_socket_inode_lock(inode) };
        }

        // SAFETY: A live socket operation table supplies the release callback.
        if let Some(release) = unsafe { (*ops).release } {
            // SAFETY: The protocol callback owns the live socket transition.
            unsafe { release(sock) };
            record(COUNTER_RELEASE);
        }

        // SAFETY: The release callback has returned and the caller still owns
        // the socket storage.
        unsafe { (*sock).sk = core::ptr::null_mut() };

        if !inode.is_null() {
            // SAFETY: This function acquired the inode lock above.
            unsafe { bindings::rust_helper_socket_inode_unlock(inode) };
        }

        // SAFETY: The protocol release callback has completed.
        unsafe { (*sock).ops = core::ptr::null() };
        // SAFETY: The operation table reference was acquired at creation.
        unsafe { bindings::module_put(owner) };
    }

    // SAFETY: The helper only reads the socket wait queue state.
    if unsafe { bindings::rust_helper_socket_fasync_pending(sock) } {
        // SAFETY: The helper only emits the original diagnostic.
        unsafe { bindings::rust_helper_socket_warn_fasync() };
    }

    // SAFETY: The caller supplies live socket storage.
    if unsafe { (*sock).file.is_null() } {
        // SAFETY: The socket owns the embedded inode when it has no file.
        let inode = unsafe { bindings::rust_helper_socket_inode_from_socket(sock) };
        // SAFETY: The socket inode owns the final VFS reference.
        unsafe { bindings::iput(inode) };
    } else {
        // SAFETY: The socket is still live and the file owns the final inode
        // reference.
        unsafe { bindings::rust_helper_socket_clear_file(sock) };
    }
}

/// Defines `sock_alloc()` in Rust.
///
/// # Safety
///
/// This function may sleep and must run in process context.
#[export]
unsafe extern "C" fn sock_alloc() -> *mut Socket {
    // SAFETY: The helper allocates a socketfs pseudo inode.
    let inode = unsafe { bindings::rust_helper_socket_new_pseudo_inode() };
    if inode.is_null() {
        return core::ptr::null_mut();
    }

    // SAFETY: The helper initialized a new unreferenced inode.
    unsafe { bindings::rust_helper_socket_init_inode(inode) };
    // SAFETY: The socket is embedded in the socket inode allocation.
    unsafe { bindings::rust_helper_socket_from_inode(inode) }
}

/// Defines `sock_alloc_file()` in Rust.
///
/// # Safety
///
/// `sock` must be a live unbound socket. `dname` must be null or point to a
/// live NUL-terminated string. The caller transfers the socket on failure.
#[export]
unsafe extern "C" fn sock_alloc_file(
    sock: *mut Socket,
    flags: c_int,
    dname: *const u8,
) -> *mut File {
    let dname = if dname.is_null() {
        // SAFETY: The caller supplies a live socket and name.
        unsafe { bindings::rust_helper_socket_protocol_name(sock) }
    } else {
        dname
    };

    // SAFETY: The socket owns its embedded inode and socketfs mount reference.
    let (inode, mount, fops) = unsafe {
        (
            bindings::rust_helper_socket_inode_from_socket(sock),
            bindings::rust_helper_socket_mount(),
            bindings::rust_helper_socket_file_operations(),
        )
    };
    let open_flags = O_RDWR | (flags & O_NONBLOCK);

    // SAFETY: The helper retains the original pseudo-file allocation contract.
    let file = unsafe { bindings::alloc_file_pseudo(inode, mount, dname, open_flags, fops) };
    if is_err(file.cast()) {
        // SAFETY: The failed file binding leaves socket ownership with this
        // function.
        unsafe { sock_release(sock) };
        return file;
    }

    // SAFETY: The new file and socket are live and exclusively owned.
    unsafe {
        bindings::rust_helper_socket_file_set_nowait(file);
        bindings::rust_helper_socket_bind_file(sock, file);
        bindings::rust_helper_socket_stream_open(sock, file);
        bindings::rust_helper_socket_file_set_fsnotify_mode(file);
    }
    file
}

/// Binds a live socket to an unused file descriptor.
///
/// # Safety
///
/// `sock` must be a live unbound socket. The caller transfers the socket.
unsafe fn sock_map_fd(sock: *mut Socket, flags: c_int) -> c_int {
    // SAFETY: The descriptor table primitive reserves one process slot.
    let fd = unsafe { bindings::get_unused_fd_flags(flags as c_uint) };
    if fd < 0 {
        // SAFETY: No descriptor was installed and the caller transferred the
        // socket.
        unsafe { sock_release(sock) };
        return fd;
    }

    // SAFETY: The socket remains live until the file allocation completes.
    let file = unsafe { sock_alloc_file(sock, flags, core::ptr::null()) };
    if !is_err(file.cast()) {
        // SAFETY: The file owns the socket and the reserved descriptor slot is
        // empty.
        unsafe { bindings::fd_install(fd as c_uint, file) };
        return fd;
    }

    // SAFETY: The reserved descriptor remains empty.
    unsafe { bindings::put_unused_fd(fd as c_uint) };
    // SAFETY: The helper decodes the original error pointer.
    unsafe { bindings::rust_helper_socket_ptr_err(file.cast()) as c_int }
}

/// Defines `sock_release()` in Rust.
///
/// # Safety
///
/// `sock` must be a live socket allocation.
#[export]
unsafe extern "C" fn sock_release(sock: *mut Socket) {
    // SAFETY: The caller transfers the live socket.
    unsafe { socket_release_inner(sock, core::ptr::null_mut()) };
}

/// Closes a socket file.
///
/// # Safety
///
/// `inode` must be the live socket inode. `file` must be its live file.
#[no_mangle]
unsafe extern "C" fn rust_sock_close(inode: *mut Inode, _file: *mut File) -> c_int {
    // SAFETY: The socket is embedded in the socket inode.
    let sock = unsafe { bindings::rust_helper_socket_from_inode(inode) };
    // SAFETY: The file release callback holds the final file and inode
    // references.
    unsafe { socket_release_inner(sock, inode) };
    0
}

/// Defines `sock_create_lite()` in Rust.
///
/// # Safety
///
/// `res` must point to writable storage. This function may sleep.
#[export]
unsafe extern "C" fn sock_create_lite(
    family: c_int,
    type_: c_int,
    protocol: c_int,
    res: *mut *mut Socket,
) -> c_int {
    // SAFETY: The helper invokes the original LSM creation hook.
    let mut error =
        unsafe { bindings::rust_helper_socket_security_create(family, type_, protocol, 1) };
    let mut sock = core::ptr::null_mut();

    if error == 0 {
        // SAFETY: The helper allocates a socketfs inode.
        sock = unsafe { sock_alloc() };
        if sock.is_null() {
            error = negative(bindings::ENOMEM);
        } else {
            // SAFETY: The new socket is exclusively owned.
            unsafe { (*sock).type_ = type_ as i16 };
            // SAFETY: The helper invokes the original LSM post-create hook.
            error = unsafe {
                bindings::rust_helper_socket_security_post_create(sock, family, type_, protocol, 1)
            };
            if error != 0 {
                // SAFETY: The failed creation path still owns the socket.
                unsafe { sock_release(sock) };
                sock = core::ptr::null_mut();
            }
        }
    }

    // SAFETY: The caller supplied writable output storage.
    unsafe { *res = sock };
    error
}

/// Defines `__sock_create()` in Rust.
///
/// # Safety
///
/// `net` must be a live network namespace. `res` must point to writable
/// storage. This function may sleep.
#[export]
unsafe extern "C" fn __sock_create(
    net: *mut Net,
    mut family: c_int,
    type_: c_int,
    protocol: c_int,
    res: *mut *mut Socket,
    kern: c_int,
) -> c_int {
    if !(0..NPROTO).contains(&family) {
        return negative(bindings::EAFNOSUPPORT);
    }
    if !(0..SOCK_MAX).contains(&type_) {
        return negative(bindings::EINVAL);
    }

    if family == PF_INET && type_ == SOCK_PACKET {
        // SAFETY: The compatibility warning uses only current-task state.
        unsafe { bindings::rust_helper_socket_warn_obsolete_packet() };
        family = PF_PACKET;
    }

    // SAFETY: The helper invokes the original LSM creation hook.
    let error =
        unsafe { bindings::rust_helper_socket_security_create(family, type_, protocol, kern) };
    if error != 0 {
        return error;
    }

    // SAFETY: The helper allocates a socketfs inode.
    let sock = unsafe { sock_alloc() };
    if sock.is_null() {
        // SAFETY: The helper preserves the original warning.
        unsafe { bindings::rust_helper_socket_warn_no_sockets() };
        return negative(bindings::ENFILE);
    }

    // SAFETY: The new socket is exclusively owned.
    unsafe { (*sock).type_ = type_ as i16 };
    // SAFETY: The helper requests the protocol module if it is not loaded.
    unsafe { bindings::rust_helper_socket_request_family(family as c_uint) };

    // SAFETY: The helper enters the RCU read-side critical section used for the
    // family registry.
    unsafe { bindings::rust_helper_socket_rcu_read_lock() };
    // SAFETY: This function holds the RCU read-side critical section.
    let family_ops = unsafe { bindings::rust_helper_socket_family_lookup(family as c_uint) };
    if family_ops.is_null() || injected(FAIL_FAMILY) {
        // SAFETY: This function entered the RCU read-side critical section.
        unsafe { bindings::rust_helper_socket_rcu_read_unlock() };
        // SAFETY: The failed path owns the socket.
        unsafe { sock_release(sock) };
        return negative(bindings::EAFNOSUPPORT);
    }

    // SAFETY: The registry entry is protected by this RCU section.
    let owner = unsafe { (*family_ops).owner };
    // SAFETY: The module reference keeps the family callbacks live after RCU
    // unlock.
    let module_live = !injected(FAIL_FAMILY_MODULE) && unsafe { bindings::try_module_get(owner) };
    if !module_live {
        // SAFETY: This function entered the RCU read-side critical section.
        unsafe { bindings::rust_helper_socket_rcu_read_unlock() };
        // SAFETY: The failed path owns the socket.
        unsafe { sock_release(sock) };
        return negative(bindings::EAFNOSUPPORT);
    }

    // SAFETY: The module reference protects the registry entry.
    unsafe { bindings::rust_helper_socket_rcu_read_unlock() };

    // SAFETY: The module reference keeps the callback live.
    let create = unsafe { (*family_ops).create };
    let mut error = if injected(FAIL_CREATE) {
        negative(bindings::EAFNOSUPPORT)
    } else if let Some(create) = create {
        // SAFETY: The callback owns protocol initialization for the live
        // socket.
        unsafe { create(net, sock, protocol, kern) }
    } else {
        negative(bindings::EAFNOSUPPORT)
    };

    if error < 0 {
        // SAFETY: The callback contract requires sock->sk to be null on
        // failure.
        unsafe {
            bindings::rust_helper_socket_warn_create_sk(sock, family_ops, family, type_, protocol);
            (*sock).ops = core::ptr::null();
            bindings::module_put(owner);
            sock_release(sock);
        }
        return error;
    }

    // SAFETY: A successful protocol callback installs a live operation table.
    let ops = unsafe { bindings::rust_helper_socket_ops(sock) };
    if ops.is_null() {
        // SAFETY: The family module reference and socket remain owned here.
        unsafe {
            (*sock).ops = core::ptr::null();
            bindings::module_put(owner);
            sock_release(sock);
        }
        return negative(bindings::EAFNOSUPPORT);
    }

    // SAFETY: The protocol callback installed the operation table.
    let ops_owner = unsafe { (*ops).owner };
    // SAFETY: The operation owner reference covers socket release.
    if injected(FAIL_OPS_MODULE) || !unsafe { bindings::try_module_get(ops_owner) } {
        // SAFETY: The family module reference and socket remain owned here.
        unsafe {
            (*sock).ops = core::ptr::null();
            bindings::module_put(owner);
            sock_release(sock);
        }
        return negative(bindings::EAFNOSUPPORT);
    }

    // SAFETY: The operation table now owns a module reference.
    unsafe { bindings::module_put(owner) };

    // SAFETY: The helper invokes the original LSM post-create hook.
    error = unsafe {
        bindings::rust_helper_socket_security_post_create(sock, family, type_, protocol, kern)
    };
    if error != 0 || injected(FAIL_SECURITY_POST) {
        // SAFETY: The failed post-create path owns the socket and operation
        // reference.
        unsafe { sock_release(sock) };
        return if error == 0 {
            negative(bindings::EPERM)
        } else {
            error
        };
    }

    // SAFETY: The caller supplied writable output storage.
    unsafe { *res = sock };
    0
}

/// Defines `sock_create()` in Rust.
///
/// # Safety
///
/// `res` must point to writable storage. This function may sleep.
#[export]
unsafe extern "C" fn sock_create(
    family: c_int,
    type_: c_int,
    protocol: c_int,
    res: *mut *mut Socket,
) -> c_int {
    // SAFETY: The helper reads the current task network namespace.
    let net = unsafe { bindings::rust_helper_socket_current_net() };
    // SAFETY: The live namespace and output storage satisfy the inner
    // contract.
    unsafe { __sock_create(net, family, type_, protocol, res, 0) }
}

/// Defines `sock_create_kern()` in Rust.
///
/// # Safety
///
/// `net` must be a live network namespace. `res` must point to writable
/// storage. This function may sleep.
#[export]
unsafe extern "C" fn sock_create_kern(
    net: *mut Net,
    family: c_int,
    type_: c_int,
    protocol: c_int,
    res: *mut *mut Socket,
) -> c_int {
    // SAFETY: The live namespace and output storage satisfy the inner
    // contract.
    unsafe { __sock_create(net, family, type_, protocol, res, 1) }
}

/// Creates a socket and optional file for a syscall-level request.
///
/// # Safety
///
/// This function may sleep.
unsafe fn sys_socket_create(family: c_int, type_: c_int, protocol: c_int) -> *mut Socket {
    let flags = type_ & !SOCK_TYPE_MASK;
    if flags & !(SOCK_CLOEXEC | SOCK_NONBLOCK) != 0 {
        return err_ptr(-(bindings::EINVAL as isize)).cast();
    }

    let mut sock = core::ptr::null_mut();
    // SAFETY: The output storage is local and the type has been normalized.
    let error = unsafe { sock_create(family, type_ & SOCK_TYPE_MASK, protocol, &mut sock) };
    if error < 0 {
        return err_ptr(error as isize).cast();
    }
    sock
}

fn syscall_flags(type_: c_int) -> c_int {
    let mut flags = type_ & !SOCK_TYPE_MASK;
    if SOCK_NONBLOCK != O_NONBLOCK && flags & SOCK_NONBLOCK != 0 {
        flags = (flags & !SOCK_NONBLOCK) | O_NONBLOCK;
    }
    flags
}

/// Defines `__sys_socket_file()` in Rust.
///
/// # Safety
///
/// This function may sleep and returns an owned file or an error pointer.
#[export]
unsafe extern "C" fn __sys_socket_file(family: c_int, type_: c_int, protocol: c_int) -> *mut File {
    // SAFETY: The helper validates and creates a live socket.
    let sock = unsafe { sys_socket_create(family, type_, protocol) };
    if is_err(sock.cast()) {
        return sock.cast();
    }

    // SAFETY: The created socket is exclusively owned.
    unsafe { sock_alloc_file(sock, syscall_flags(type_), core::ptr::null()) }
}

/// Defines `__sys_socket()` in Rust.
///
/// # Safety
///
/// This function may sleep.
#[export]
unsafe extern "C" fn __sys_socket(family: c_int, type_: c_int, protocol: c_int) -> c_int {
    // SAFETY: The BPF hook has the original protocol override contract.
    let protocol = unsafe { bindings::rust_helper_socket_update_protocol(family, type_, protocol) };
    // SAFETY: The helper validates and creates a live socket.
    let sock = unsafe { sys_socket_create(family, type_, protocol) };
    if is_err(sock.cast()) {
        // SAFETY: The helper decodes the socket error pointer.
        return unsafe { bindings::rust_helper_socket_ptr_err(sock.cast()) as c_int };
    }

    let flags = syscall_flags(type_);
    // SAFETY: The created socket is exclusively owned.
    unsafe { sock_map_fd(sock, flags & (O_CLOEXEC | O_NONBLOCK)) }
}

/// Defines `sk_alloc()` in Rust.
///
/// # Safety
///
/// `net` must be live. `prot` must remain live for the returned sock lifetime.
#[export]
unsafe extern "C" fn sk_alloc(
    net: *mut Net,
    family: c_int,
    priority: Gfp,
    prot: *mut Proto,
    kern: c_int,
) -> *mut Sock {
    // SAFETY: The helper performs the original slab or kmalloc allocation.
    let sk =
        unsafe { bindings::rust_helper_sock_prot_alloc(prot, priority | bindings::__GFP_ZERO) };
    if sk.is_null() {
        return core::ptr::null_mut();
    }

    // SAFETY: The allocation is live and uninitialized.
    if unsafe { bindings::rust_helper_sock_security_alloc(sk, family, priority) } != 0 {
        // SAFETY: Security initialization failed before any other resource
        // was attached.
        unsafe { bindings::rust_helper_sock_prot_free(prot, sk) };
        return core::ptr::null_mut();
    }

    // SAFETY: The protocol module reference keeps prot and its callbacks live.
    if !unsafe { bindings::rust_helper_sock_module_get(prot) } {
        // SAFETY: The security allocation is the only attached resource.
        unsafe {
            bindings::rust_helper_sock_security_free(sk);
            bindings::rust_helper_sock_prot_free(prot, sk);
        }
        return core::ptr::null_mut();
    }

    // SAFETY: All allocation stages succeeded and the object is exclusively
    // owned.
    unsafe {
        bindings::rust_helper_sock_set_family(sk, family);
        bindings::rust_helper_sock_set_prot(sk, prot);
        if bindings::rust_helper_sock_bypass(net) {
            bindings::rust_helper_sock_set_bypass(sk, true);
        }
        bindings::rust_helper_sock_set_kern(sk, kern != 0);
        bindings::rust_helper_sock_lock_init(sk);
    }

    if kern == 0 {
        // SAFETY: The new object is exclusively owned.
        unsafe {
            bindings::rust_helper_sock_set_net_refcnt(sk, true);
            bindings::rust_helper_sock_net_get_tracked(sk, net, priority);
            bindings::rust_helper_sock_inuse_add(net, 1);
        }
    } else {
        // SAFETY: The new object is exclusively owned.
        unsafe {
            bindings::rust_helper_sock_set_net_refcnt(sk, false);
            bindings::rust_helper_sock_net_passive_inc(net);
            bindings::rust_helper_sock_net_tracker_alloc(sk, net, false, priority);
        }
    }

    // SAFETY: The new object is exclusively owned.
    unsafe {
        bindings::rust_helper_sock_net_set(sk, net);
        bindings::rust_helper_sock_wmem_init(sk);
        bindings::rust_helper_sock_memcg_alloc(sk);
        bindings::rust_helper_sock_cgroup_alloc(sk);
        bindings::rust_helper_sock_update_classid(sk);
        bindings::rust_helper_sock_update_netprioidx(sk);
        bindings::rust_helper_sock_tx_queue_clear(sk);
    }

    record(COUNTER_SK_ALLOC);
    sk
}

/// Releases the final sock resource set.
///
/// # Safety
///
/// `sk` must be the final live reference and must not be accessed afterward.
unsafe fn sock_destruct_final(sk: *mut Sock) {
    // SAFETY: The final reference owns the protocol destructor and all
    // descendant resources.
    unsafe { bindings::rust_helper_sock_call_user_destruct(sk) };

    // SAFETY: The filter pointer is protected by the zero wmem allocation
    // condition used by the original destructor.
    let filter = unsafe { bindings::rust_helper_sock_filter(sk) };
    // SAFETY: The helper checks the pointer and releases its accounting.
    unsafe { bindings::rust_helper_sock_filter_uncharge(sk, filter) };
    // SAFETY: The final reference owns timestamp cleanup.
    unsafe { bindings::rust_helper_sock_disable_timestamp(sk) };
    // SAFETY: The final reference owns optional BPF sk storage.
    unsafe { bindings::rust_helper_sock_bpf_storage_free(sk) };
    // SAFETY: The helper emits and classifies the original leak diagnostic.
    let _ = unsafe { bindings::rust_helper_sock_omem_leak(sk) };
    // SAFETY: The final reference owns the socket frag page.
    unsafe { bindings::rust_helper_sock_put_frag(sk) };
    // SAFETY: The final reference owns peer credentials and pid.
    unsafe { bindings::rust_helper_sock_put_peer_resources(sk) };

    // SAFETY: The final reference owns the net namespace accounting.
    let (net, net_refcnt) = unsafe {
        (
            bindings::rust_helper_sock_net(sk),
            bindings::rust_helper_sock_net_refcnt(sk),
        )
    };
    if net_refcnt {
        // SAFETY: The sock holds a tracked network namespace reference.
        unsafe { bindings::rust_helper_sock_put_net_tracked(sk, net) };
    } else {
        // SAFETY: Kernel sockets hold only a passive namespace reference.
        unsafe {
            bindings::rust_helper_sock_net_tracker_free(sk, net, false);
            bindings::rust_helper_sock_net_passive_dec(net);
        }
    }

    // SAFETY: All sock fields that need protocol resources are released.
    let prot = unsafe { bindings::rust_helper_sock_prot_creator(sk) };
    // SAFETY: The final reference owns cgroup, memcg, security, and owner
    // references.
    unsafe {
        bindings::rust_helper_sock_cgroup_free(sk);
        bindings::rust_helper_sock_memcg_free(sk);
        bindings::rust_helper_sock_security_free(sk);
        bindings::rust_helper_sock_owner_put(sk);
        bindings::rust_helper_sock_prot_free(prot, sk);
        bindings::rust_helper_sock_module_put(prot);
    }
    record(COUNTER_SK_FREE);
}

/// RCU callback for the final sock destruction.
///
/// # Safety
///
/// `head` must be the embedded RCU head of a sock that reached its final
/// reference.
#[no_mangle]
unsafe extern "C" fn rust_socket_sk_destruct_rcu(head: *mut CallbackHead) {
    // SAFETY: The callback receives the embedded sock RCU head.
    let sk = unsafe { bindings::rust_helper_sock_from_rcu(head) };
    record(COUNTER_RCU_FREE);
    // SAFETY: The RCU callback owns the final sock reference.
    unsafe { sock_destruct_final(sk) };
}

/// Defines `sk_destruct()` in Rust.
///
/// # Safety
///
/// `sk` must have no external references and must not be accessed after this
/// call.
#[export]
unsafe extern "C" fn sk_destruct(sk: *mut Sock) {
    // SAFETY: The caller transferred the final sock reference.
    let mut use_rcu = unsafe { bindings::rust_helper_sock_rcu_free(sk) };
    // SAFETY: The helper checks the RCU reusable-port callback.
    if unsafe { bindings::rust_helper_sock_has_reuseport(sk) } {
        // SAFETY: No external references remain, so detach can consume the
        // reusable-port link.
        unsafe { bindings::rust_helper_sock_reuseport_detach(sk) };
        use_rcu = true;
    }

    if use_rcu {
        // SAFETY: The sock remains allocated until the RCU callback runs.
        unsafe { bindings::rust_helper_sock_call_rcu(sk, Some(rust_socket_sk_destruct_rcu)) };
    } else {
        // SAFETY: The caller owns the final sock reference.
        unsafe { sock_destruct_final(sk) };
    }
}

/// Completes final sock release after the write-memory reference reaches zero.
///
/// # Safety
///
/// `sk` must have no external references and must not be accessed afterward.
#[no_mangle]
unsafe extern "C" fn rust_socket_sk_free_final(sk: *mut Sock) {
    // SAFETY: The caller transferred the final sock reference.
    let net_refcnt = unsafe { bindings::rust_helper_sock_net_refcnt(sk) };
    if net_refcnt {
        // SAFETY: The final sock reference owns one in-use count.
        let net = unsafe { bindings::rust_helper_sock_net(sk) };
        // SAFETY: The final sock reference owns one in-use count.
        unsafe { bindings::rust_helper_sock_inuse_add(net, -1) };
    }

    // SAFETY: The final sock reference owns diagnostic publication state.
    let diag_listener = net_refcnt && unsafe { bindings::rust_helper_sock_diag_has_listeners(sk) };
    if diag_listener {
        // SAFETY: The final sock reference owns the diagnostic publication.
        unsafe { bindings::rust_helper_sock_diag_broadcast_destroy(sk) };
    } else {
        // SAFETY: The final sock reference owns the destructor transition.
        unsafe { sk_destruct(sk) };
    }
}

/// Defines `sk_free()` in Rust.
///
/// # Safety
///
/// `sk` must have no external references. In-flight transmit buffers may still
/// hold write-memory accounting.
#[export]
unsafe extern "C" fn sk_free(sk: *mut Sock) {
    // SAFETY: The helper performs the original write-memory reference drop.
    if unsafe { bindings::rust_helper_sock_wmem_put(sk) } {
        // SAFETY: No external references and no in-flight write buffers
        // remain.
        unsafe { rust_socket_sk_free_final(sk) };
    }
}

/// Defines the out-of-line `sock_put()` used by built-in C code.
///
/// Modules retain the original inline implementation and therefore do not gain
/// a new module ABI dependency.
///
/// # Safety
///
/// `sk` must contain a live socket reference owned by the caller.
#[no_mangle]
unsafe extern "C" fn sock_put(sk: *mut Sock) {
    // SAFETY: The caller transfers one socket reference.
    if unsafe { bindings::rust_helper_sock_ref_put(sk) } {
        // SAFETY: The caller transferred the final socket reference.
        unsafe { sk_free(sk) };
    }
}
