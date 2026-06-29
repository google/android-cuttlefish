//
// Copyright (C) 2024 The Android Open Source Project
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

//! Weaver TA core for Cuttlefish.

use std::os::fd::OwnedFd;

/// FFI wrapper around [`weaver_cf::ta_main`].
///
/// # Safety
///
/// `fd_in`, `fd_out` and `snapshot_fd` must be valid and open file descriptors and the caller must
/// not use or close them after the call. `storage_path` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn weaver_ta_main(
    fd_in: OwnedFd,
    fd_out: OwnedFd,
    storage_path: *const libc::c_char,
    snapshot_fd: OwnedFd,
) {
    // SAFETY: The caller guarantees `storage_path` is a valid null-terminated C string.
    let storage_path =
        unsafe { std::ffi::CStr::from_ptr(storage_path) }.to_string_lossy().into_owned();
    let snapshot_socket = std::os::unix::net::UnixStream::from(snapshot_fd);
    weaver_cf::ta_main(fd_in.into(), fd_out.into(), storage_path, snapshot_socket)
}
