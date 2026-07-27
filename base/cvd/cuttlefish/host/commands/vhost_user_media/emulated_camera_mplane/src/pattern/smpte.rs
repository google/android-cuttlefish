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

const SMPTE_COLOR_WHITE: (u8, u8, u8) = (180, 128, 128);
const SMPTE_COLOR_YELLOW: (u8, u8, u8) = (162, 44, 142);
const SMPTE_COLOR_CYAN: (u8, u8, u8) = (131, 156, 44);
const SMPTE_COLOR_GREEN: (u8, u8, u8) = (112, 72, 58);
const SMPTE_COLOR_MAGENTA: (u8, u8, u8) = (84, 184, 198);
const SMPTE_COLOR_RED: (u8, u8, u8) = (65, 100, 212);
const SMPTE_COLOR_BLUE: (u8, u8, u8) = (35, 212, 114);
const SMPTE_COLOR_BLACK: (u8, u8, u8) = (16, 128, 128);
const SMPTE_COLOR_DARK_GRAY: (u8, u8, u8) = (25, 128, 128);

pub struct SmpteBars;

impl FramePattern for SmpteBars {
    fn write<WY: Write, WU: Write, WV: Write>(
        &self,
        iteration: u64,
        mut sink_y: WY,
        mut sink_u: WU,
        mut sink_v: WV,
    ) -> Result<(), i32> {
        let sequence = iteration;
        let box_size = 80u32; // Scaled for 640x480

        // Helper for triangle wave (constant velocity bounce)
        let bouncing_box_coord = |t: u64, range: u32| -> u32 {
            let range64 = range as u64;
            let period = 2 * range64;
            let val = t % period;
            (if val < range64 { val } else { period - val }) as u32
        };

        let box_x = bouncing_box_coord(sequence * 8, WIDTH - box_size);
        let box_y = bouncing_box_coord(sequence * 5, HEIGHT - box_size);

        let is_inside_box = |x: u32, y: u32| -> bool {
            x >= box_x && x < box_x + box_size && y >= box_y && y < box_y + box_size
        };

        let get_smpte_color = |x: u32, y: u32| -> (u8, u8, u8) {
            let bars = [
                SMPTE_COLOR_WHITE,
                SMPTE_COLOR_YELLOW,
                SMPTE_COLOR_CYAN,
                SMPTE_COLOR_GREEN,
                SMPTE_COLOR_MAGENTA,
                SMPTE_COLOR_RED,
                SMPTE_COLOR_BLUE,
                SMPTE_COLOR_BLACK,
            ];
            let bar_width = WIDTH / 7;
            let bar_idx = std::cmp::min(x / bar_width, 6) as usize;

            let row1_height = HEIGHT * 2 / 3;
            let row2_height = HEIGHT * 3 / 4;

            if y < row1_height {
                bars[bar_idx]
            } else if y < row2_height {
                // Reversed bars for middle row
                bars[6 - bar_idx]
            } else {
                // Bottom row blocks
                if x < bar_width {
                    SMPTE_COLOR_BLUE
                } else if x < bar_width * 2 {
                    SMPTE_COLOR_WHITE
                } else if x < bar_width * 3 {
                    SMPTE_COLOR_MAGENTA
                } else if x < bar_width * 4 {
                    SMPTE_COLOR_BLACK
                } else {
                    SMPTE_COLOR_DARK_GRAY
                }
            }
        };

        // Write Y Plane
        let mut y_plane = Vec::with_capacity((WIDTH * HEIGHT) as usize);
        for y_idx in 0..HEIGHT {
            for x_idx in 0..WIDTH {
                let (y, _, _) = get_smpte_color(x_idx, y_idx);
                let is_box = is_inside_box(x_idx, y_idx);
                y_plane.push(if is_box { 255 - y } else { y });
            }
        }
        sink_y.write_all(&y_plane).map_err(|_| libc::EIO)?;

        // Write U Plane
        let mut u_plane = Vec::with_capacity((WIDTH * HEIGHT / 4) as usize);
        for y_idx in 0..(HEIGHT / 2) {
            for x_idx in 0..(WIDTH / 2) {
                let (_, u, _) = get_smpte_color(x_idx * 2, y_idx * 2);
                let is_box = is_inside_box(x_idx * 2, y_idx * 2);
                u_plane.push(if is_box { 255 - u } else { u });
            }
        }
        sink_u.write_all(&u_plane).map_err(|_| libc::EIO)?;

        // Write V Plane
        let mut v_plane = Vec::with_capacity((WIDTH * HEIGHT / 4) as usize);
        for y_idx in 0..(HEIGHT / 2) {
            for x_idx in 0..(WIDTH / 2) {
                let (_, _, v) = get_smpte_color(x_idx * 2, y_idx * 2);
                let is_box = is_inside_box(x_idx * 2, y_idx * 2);
                v_plane.push(if is_box { 255 - v } else { v });
            }
        }
        sink_v.write_all(&v_plane).map_err(|_| libc::EIO)?;

        Ok(())
    }
}
