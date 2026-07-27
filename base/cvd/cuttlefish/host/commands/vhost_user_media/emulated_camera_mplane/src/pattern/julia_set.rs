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

pub struct JuliaSet;

impl FramePattern for JuliaSet {
    fn write<WY: Write, WU: Write, WV: Write>(
        &self,
        iteration: u64,
        mut sink_y: WY,
        mut sink_u: WU,
        mut sink_v: WV,
    ) -> Result<(), i32> {
        let sequence = iteration;
        let max_iter = 32usize;
        let angle = (sequence as f32) * 0.04f32;
        let c_re = 0.7885f32 * angle.cos();
        let c_im = 0.7885f32 * angle.sin();

        // Write Y Plane (Fractal Detail)
        let mut y_plane = Vec::with_capacity((WIDTH * HEIGHT) as usize);
        for y_idx in 0..HEIGHT as usize {
            for x_idx in 0..WIDTH as usize {
                let mut z_re = 1.5f32 * (x_idx as f32 - WIDTH as f32 / 2.0) / (0.5 * WIDTH as f32);
                let mut z_im = (y_idx as f32 - HEIGHT as f32 / 2.0) / (0.5 * HEIGHT as f32);
                let mut iter = 0usize;
                while z_re * z_re + z_im * z_im < 4.0 && iter < max_iter {
                    let next_re = z_re * z_re - z_im * z_im + c_re;
                    z_im = 2.0 * z_re * z_im + c_im;
                    z_re = next_re;
                    iter += 1;
                }
                y_plane.push((iter as u8 * 7).wrapping_add(16));
            }
        }
        sink_y.write_all(&y_plane).map_err(|_| libc::EIO)?;

        // Write U/V Planes (Constant Neutral)
        let uv_size = (WIDTH * HEIGHT / 4) as usize;
        let u_plane = vec![128u8; uv_size];
        let v_plane = vec![128u8; uv_size];
        sink_u.write_all(&u_plane).map_err(|_| libc::EIO)?;
        sink_v.write_all(&v_plane).map_err(|_| libc::EIO)?;

        Ok(())
    }
}
