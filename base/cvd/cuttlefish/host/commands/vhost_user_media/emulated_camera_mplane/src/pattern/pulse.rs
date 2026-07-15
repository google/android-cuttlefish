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

use super::{FramePattern, HEIGHT, WIDTH};
use std::io::Write;

/// Fills the whole frame with a single color that cycles as frames are produced.
pub struct Pulse;

impl FramePattern for Pulse {
    fn write(
        &self,
        iteration: u64,
        sink_y: &mut dyn Write,
        sink_u: &mut dyn Write,
        sink_v: &mut dyn Write,
    ) -> Result<(), i32> {
        let sequence = iteration;
        let y = (sequence % 256) as u8;
        let u = ((sequence + 64) % 256) as u8;
        let v = ((sequence + 128) % 256) as u8;
        let y_plane = vec![y; (WIDTH * HEIGHT) as usize];
        let u_plane = vec![u; (WIDTH * HEIGHT / 4) as usize];
        let v_plane = vec![v; (WIDTH * HEIGHT / 4) as usize];
        sink_y.write_all(&y_plane).map_err(|_| libc::EIO)?;
        sink_u.write_all(&u_plane).map_err(|_| libc::EIO)?;
        sink_v.write_all(&v_plane).map_err(|_| libc::EIO)?;
        Ok(())
    }
}
