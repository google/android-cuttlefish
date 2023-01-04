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

/// FFI wrapper around [`kmr_cf::ta_main`].
#[no_mangle]
pub extern "C" fn kmr_ta_main(
    fd_in: c_int,
    fd_out: c_int,
    security_level: c_int,
    trm: *mut libc::c_void,
) {
    let security_level = match security_level as i32 {
        x if x == SecurityLevel::TrustedEnvironment as i32 => SecurityLevel::TrustedEnvironment,
        x if x == SecurityLevel::Strongbox as i32 => SecurityLevel::Strongbox,
        x if x == SecurityLevel::Software as i32 => SecurityLevel::Software,
        _ => {
            error!("unexpected security level {}, running as SOFTWARE", security_level);
            SecurityLevel::Software
        }
    };
    kmr_cf::ta_main(fd_in, fd_out, security_level, trm)
}
