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

pub mod pulse;

/// Frame geometry is owned by the device, which advertises it to the guest through
/// `v4l2_format`. Patterns re-export it so that both always agree on the plane sizes.
pub(crate) use crate::device::HEIGHT;
pub(crate) use crate::device::WIDTH;

/// Generator for a single multi-planar YUV 4:2:0 frame.
///
/// The trait is kept object safe so that the active pattern can be selected at runtime
/// and held as a `&dyn FramePattern`.
pub trait FramePattern {
    /// Writes the frame identified by `iteration` into the three plane sinks.
    ///
    /// Implementations must write exactly `WIDTH * HEIGHT` bytes to `sink_y` and
    /// `WIDTH * HEIGHT / 4` bytes to each of `sink_u` and `sink_v`, which is the plane
    /// size the device advertised to the guest.
    ///
    /// Returns a raw `errno` value on failure.
    fn write(
        &self,
        iteration: u64,
        sink_y: &mut dyn Write,
        sink_u: &mut dyn Write,
        sink_v: &mut dyn Write,
    ) -> Result<(), i32>;
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Renders a few frames and checks that each plane is exactly the size the device
    /// advertised. A short write would leave stale bytes in the guest buffer.
    fn assert_plane_sizes(pattern: &dyn FramePattern) {
        for iteration in [0u64, 1, 255, 4096] {
            let mut plane_y = Vec::new();
            let mut plane_u = Vec::new();
            let mut plane_v = Vec::new();
            pattern
                .write(iteration, &mut plane_y, &mut plane_u, &mut plane_v)
                .expect("writing into a Vec cannot fail");
            assert_eq!(plane_y.len(), (WIDTH * HEIGHT) as usize);
            assert_eq!(plane_u.len(), (WIDTH * HEIGHT / 4) as usize);
            assert_eq!(plane_v.len(), (WIDTH * HEIGHT / 4) as usize);
        }
    }

    #[test]
    fn pulse_writes_full_planes() {
        assert_plane_sizes(&pulse::Pulse);
    }
}
