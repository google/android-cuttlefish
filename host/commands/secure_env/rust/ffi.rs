//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! KeyMint TA core for Cuttlefish.

extern crate alloc;

use kmr_wire::keymint::SecurityLevel;
use libc::c_int;
use log::error;
use std::os::fd::FromRawFd;

/// FFI wrapper around [`kmr_cf::ta_main`].
///
/// # Safety
///
/// `fd_in`, `fd_out`, and `snapshot_socket_fd` must be valid and open file descriptors and the
/// caller must not use or close them after the call.
#[no_mangle]
pub unsafe extern "C" fn kmr_ta_main(
    fd_in: c_int,
    fd_out: c_int,
    security_level: c_int,
    trm: *mut libc::c_void,
    snapshot_socket_fd: c_int,
) {
    let security_level = match security_level {
        x if x == SecurityLevel::TrustedEnvironment as i32 => SecurityLevel::TrustedEnvironment,
        x if x == SecurityLevel::Strongbox as i32 => SecurityLevel::Strongbox,
        x if x == SecurityLevel::Software as i32 => SecurityLevel::Software,
        _ => {
            error!("unexpected security level {}, running as SOFTWARE", security_level);
            SecurityLevel::Software
        }
    };
    let snapshot_socket =
        // SAFETY: fd being valid and open and exclusive is asserted in the unsafe function's
        // preconditions, so this is pushed up to the caller.
        unsafe { std::os::unix::net::UnixStream::from_raw_fd(snapshot_socket_fd) };
    // SAFETY: The caller guarantees that `fd_in` and `fd_out` are valid and open.
    unsafe { kmr_cf::ta_main(fd_in, fd_out, security_level, trm, snapshot_socket) }
}
