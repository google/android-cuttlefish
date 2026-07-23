// Copyright 2026, The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use std::io::Write;

pub mod julia_set;
pub mod pulse;
pub mod smpte;

pub const WIDTH: u32 = 640;
pub const HEIGHT: u32 = 480;

pub trait FramePattern: Send + Sync {
    fn write<WY: Write, WU: Write, WV: Write>(
        &self,
        iteration: u64,
        sink_y: WY,
        sink_u: WU,
        sink_v: WV,
    ) -> Result<(), i32>;
}
