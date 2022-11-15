//! Monotonic clock implementation.

use core::convert::TryInto;
use kmr_common::crypto;
// The normal upstream version of [`std::time::Instant`] isn't quite right for use here, because it
// uses `clock_gettime(CLOCK_MONOTONIC)`, which stops ticking during a suspend (see
// https://github.com/rust-lang/rust/issues/87906).  However, the local Android version of
// std/src/sys/unit/time.rs has been patched to use `clock_gettime(CLOCK_BOOTTIME)` instead, which
// does included suspended time.
use std::time::Instant;

/// Monotonic clock.
pub struct StdClock(Instant);

impl Default for StdClock {
    fn default() -> Self {
        Self(Instant::now())
    }
}

impl crypto::MonotonicClock for StdClock {
    fn now(&self) -> crypto::MillisecondsSinceEpoch {
        let millis: i64 =
            self.0.elapsed().as_millis().try_into().expect("failed to fit timestamp in i64");
        crypto::MillisecondsSinceEpoch(millis)
    }
}
