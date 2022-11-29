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
