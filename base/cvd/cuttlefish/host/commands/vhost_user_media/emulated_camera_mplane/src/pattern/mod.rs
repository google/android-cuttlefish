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

use crate::device::CameraControls;
use std::io::Write;

pub mod julia_set;
pub mod pulse;
pub mod smpte;

/// Generator for a single multi-planar YUV 4:2:0 frame.
///
/// The trait is kept object safe so that the active pattern can be selected at runtime
/// and held as a `&dyn FramePattern`.
pub trait FramePattern {
    /// Writes the frame identified by `iteration` into the three plane sinks for the given resolution.
    ///
    /// Implementations must write exactly `width * height` bytes to `sink_y` and
    /// `width * height / 4` bytes to each of `sink_u` and `sink_v`.
    ///
    /// Returns a raw `errno` value on failure.
    fn write(
        &self,
        iteration: u64,
        controls: &CameraControls,
        width: u32,
        height: u32,
        sink_y: &mut dyn Write,
        sink_u: &mut dyn Write,
        sink_v: &mut dyn Write,
    ) -> Result<(), i32>;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::device::{Gain, LensFacing};

    /// Renders a few frames across supported resolutions and checks that each plane
    /// is exactly the expected size. A short write would leave stale bytes in the guest buffer.
    fn assert_plane_sizes(pattern: &dyn FramePattern) {
        let controls = CameraControls::new(LensFacing::Front);
        let resolutions: [(u32, u32); 4] = [(320, 240), (640, 480), (1280, 720), (1920, 1080)];
        for (width, height) in resolutions {
            for iteration in [0u64, 1, 255, 4096] {
                let mut plane_y = Vec::new();
                let mut plane_u = Vec::new();
                let mut plane_v = Vec::new();
                pattern
                    .write(
                        iteration,
                        &controls,
                        width,
                        height,
                        &mut plane_y,
                        &mut plane_u,
                        &mut plane_v,
                    )
                    .expect("writing into a Vec cannot fail");
                assert_eq!(plane_y.len(), (width * height) as usize);
                assert_eq!(plane_u.len(), (width * height / 4) as usize);
                assert_eq!(plane_v.len(), (width * height / 4) as usize);
            }
        }
    }

    #[test]
    fn pulse_writes_full_planes() {
        assert_plane_sizes(&pulse::Pulse);
    }

    #[test]
    fn pulse_applies_gain_scaling() {
        let mut controls = CameraControls::new(LensFacing::Front);
        controls.gain = Gain::new(200).unwrap(); // 2.0x gain

        let mut plane_y = Vec::new();
        let mut plane_u = Vec::new();
        let mut plane_v = Vec::new();
        // iteration 50: base_y = 50. With 2.0x gain, y should be 100.
        pulse::Pulse
            .write(
                50,
                &controls,
                320,
                240,
                &mut plane_y,
                &mut plane_u,
                &mut plane_v,
            )
            .expect("writing into Vec must succeed");
        assert_eq!(plane_y[0], 100);

        // iteration 200: base_y = 200. With 2.0x gain, 400 clamped to 255.
        let mut plane_y_clamped = Vec::new();
        pulse::Pulse
            .write(
                200,
                &controls,
                320,
                240,
                &mut plane_y_clamped,
                &mut plane_u,
                &mut plane_v,
            )
            .expect("writing into Vec must succeed");
        assert_eq!(plane_y_clamped[0], 255);
    }

    #[test]
    fn smpte_bars_writes_full_planes() {
        assert_plane_sizes(&smpte::SmpteBars);
    }

    #[test]
    fn julia_set_writes_full_planes() {
        assert_plane_sizes(&julia_set::JuliaSet);
    }

    /// The rotation wraps rather than growing without bound, so a frame far into a long
    /// streaming session still animates instead of freezing on a quantized angle.
    #[test]
    fn julia_set_keeps_animating_after_a_long_run() {
        let far_out = 50_000_000u64;
        let controls = CameraControls::new(LensFacing::Front);
        let luma_at = |iteration: u64| {
            let mut plane_y = Vec::new();
            let mut plane_u = Vec::new();
            let mut plane_v = Vec::new();
            julia_set::JuliaSet
                .write(
                    iteration,
                    &controls,
                    640,
                    480,
                    &mut plane_y,
                    &mut plane_u,
                    &mut plane_v,
                )
                .expect("writing into a Vec cannot fail");
            plane_y
        };
        assert_ne!(luma_at(far_out), luma_at(far_out + 1));
    }
}
