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
