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

use kmr_common::crypto;

/// Monotonic clock.
#[derive(Default)]
pub struct StdClock;

impl crypto::MonotonicClock for StdClock {
    fn now(&self) -> crypto::MillisecondsSinceEpoch {
        let mut time = libc::timespec { tv_sec: 0, tv_nsec: 0 };
        // Safety: `time` is a valid structure.
        let rc =
            unsafe { libc::clock_gettime(libc::CLOCK_BOOTTIME, &mut time as *mut libc::timespec) };
        if rc < 0 {
            log::warn!("failed to get time!");
            return crypto::MillisecondsSinceEpoch(0);
        }
        crypto::MillisecondsSinceEpoch((time.tv_sec * 1000) + (time.tv_nsec / 1000 / 1000))
    }
}
