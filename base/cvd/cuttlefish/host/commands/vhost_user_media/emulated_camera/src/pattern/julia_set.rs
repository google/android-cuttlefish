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

use super::FramePattern;
use crate::device::CameraControls;
use std::io::Write;

/// Escape-time iterations spent on each pixel.
const MAX_ITER: u32 = 32;
/// Luma assigned to a pixel that escapes immediately.
const LUMA_BASE: u32 = 16;
/// Luma added per escape-time iteration.
const LUMA_PER_ITER: u32 = 7;

// The luma mapping below narrows to `u8`, so the brightest pixel has to fit.
const _: () = assert!(LUMA_BASE + MAX_ITER * LUMA_PER_ITER <= u8::MAX as u32);

/// How far the constant `c` rotates per frame, in radians.
const ANGLE_STEP: f32 = 0.04;
/// Frames per full rotation. Wrapping `iteration` here keeps `angle` small enough that
/// `f32` can still resolve `ANGLE_STEP`, which it cannot once the angle grows past a few
/// hundred thousand radians.
const PERIOD_FRAMES: u64 = (std::f32::consts::TAU / ANGLE_STEP) as u64;

/// Animated Julia set, deliberately expensive to render.
///
/// Note: Gain scaling is intentionally ignored for Julia Set. The fractal uses a fixed
/// mathematical escape-time color palette designed to test CPU load and full-spectrum
/// color transitions; applying gain would distort the intended gradient contrast.
pub struct JuliaSet;

impl FramePattern for JuliaSet {
    fn write(
        &self,
        iteration: u64,
        // Gain is intentionally ignored for Julia Set to preserve the intended escape-time gradient contrast.
        _controls: &CameraControls,
        width: u32,
        height: u32,
        sink_y: &mut dyn Write,
        sink_u: &mut dyn Write,
        sink_v: &mut dyn Write,
    ) -> Result<(), i32> {
        let angle = (iteration % PERIOD_FRAMES) as f32 * ANGLE_STEP;
        let c_re = 0.7885f32 * angle.cos();
        let c_im = 0.7885f32 * angle.sin();

        // Write Y Plane (Fractal Detail)
        let mut y_plane = Vec::with_capacity((width * height) as usize);
        for y_idx in 0..height as usize {
            for x_idx in 0..width as usize {
                let mut z_re = 1.5f32 * (x_idx as f32 - width as f32 / 2.0) / (0.5 * width as f32);
                let mut z_im = (y_idx as f32 - height as f32 / 2.0) / (0.5 * height as f32);
                let mut iter = 0u32;
                while z_re * z_re + z_im * z_im < 4.0 && iter < MAX_ITER {
                    let next_re = z_re * z_re - z_im * z_im + c_re;
                    z_im = 2.0 * z_re * z_im + c_im;
                    z_re = next_re;
                    iter += 1;
                }
                y_plane.push((LUMA_BASE + iter * LUMA_PER_ITER) as u8);
            }
        }
        sink_y.write_all(&y_plane).map_err(|_| libc::EIO)?;

        // Write U/V Planes (Constant Neutral)
        let uv_size = (width * height / 4) as usize;
        let u_plane = vec![128u8; uv_size];
        let v_plane = vec![128u8; uv_size];
        sink_u.write_all(&u_plane).map_err(|_| libc::EIO)?;
        sink_v.write_all(&v_plane).map_err(|_| libc::EIO)?;

        Ok(())
    }
}
