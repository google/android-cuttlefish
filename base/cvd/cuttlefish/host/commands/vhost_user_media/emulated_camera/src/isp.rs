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

//! The image signal processor: turns the photo-electrons read out of the [`crate::sensor`]
//! into encoded YUV 4:2:0 planes.

use std::io::Write;

use crate::render_params::Gain;
use crate::scene::{NUM_MATERIALS, SCENE, SCENE_WIDTH};
use crate::sensor::{MAX_RAW_VALUE, Mapping, SATURATION_ELECTRONS, Sensor};

/// Fixed-point math with 6 bits of fractional precision.
const FIXED_BIT_PRECISION: i64 = 64;
/// Saturation point of the sensor after gain, in fixed point.
const SATURATION_POINT: i64 = FIXED_BIT_PRECISION * 255;

/// sRGB opto-electronic transfer function on a fixed-point value in `0..=SATURATION_POINT`.
#[inline]
fn srgb_gamma(value: i64) -> i64 {
    let n = value as f32 / SATURATION_POINT as f32;
    let n = if n <= 0.0031308 {
        n * 12.92
    } else {
        1.055 * n.powf(0.416_666_7) - 0.055
    };
    (n * SATURATION_POINT as f32) as i64
}

/// One pixel in YCbCr, 8 bits per component.
type YCbCr = [u8; 3];

/// Converts electron counts to YCbCr using the JFIF RGB->YCbCr transform.
fn electrons_to_yuv(electrons: [u32; 3], gain: Gain) -> YCbCr {
    let base_gain_factor = MAX_RAW_VALUE as f32 / SATURATION_ELECTRONS as f32;
    let total_gain = gain.value() as f32 / Gain::MIN as f32 * base_gain_factor;
    // Total scaling from electrons to 8bpp, in fixed point.
    let scale64x = (FIXED_BIT_PRECISION as f32 * total_gain * 255.0 / MAX_RAW_VALUE as f32) as i64;

    let channel = |e: u32| -> i64 { srgb_gamma((e as i64 * scale64x).min(SATURATION_POINT)) };
    let r = channel(electrons[0]);
    let g = channel(electrons[1]);
    let b = channel(electrons[2]);

    // Fixed-point JFIF RGB->YCbCr. The inputs already carry one 64x factor and the
    // coefficients another, so the 128 chroma offset is scaled by 64x twice before the
    // final division brings everything back to 8 bits.
    const SCALE_OUT_SQ: i64 = FIXED_BIT_PRECISION * FIXED_BIT_PRECISION;
    const CHROMA_OFFSET: i64 = 128 * SCALE_OUT_SQ;
    let y = (19 * r + 37 * g + 7 * b) / SCALE_OUT_SQ;
    let cb = (-10 * r - 21 * g + 32 * b + CHROMA_OFFSET) / SCALE_OUT_SQ;
    let cr = (32 * r - 26 * g - 5 * b + CHROMA_OFFSET) / SCALE_OUT_SQ;
    [
        y.clamp(0, 255) as u8,
        cb.clamp(0, 255) as u8,
        cr.clamp(0, 255) as u8,
    ]
}

/// Per-material YCbCr, indexed by `Material as usize`.
type Palette = [YCbCr; NUM_MATERIALS];

/// Writes a YUV 4:2:0 frame, looking up every pixel's material through `mapping`.
fn raster_420(
    mapping: &Mapping,
    palette: &Palette,
    width: u32,
    height: u32,
    sink_y: &mut dyn Write,
    sink_u: &mut dyn Write,
    sink_v: &mut dyn Write,
) -> Result<(), i32> {
    // Precomputed frame column -> scene column (0..20) mapping, so scene_col() does not have
    // to run again for every row; in a 640x480 frame, e.g. scene_cols_mapping[10] == 0,
    // scene_cols_mapping[250] == 7, scene_cols_mapping[639] == 19.
    let scene_cols_mapping: Vec<usize> =
        (0..width as i32).map(|frame_col| mapping.scene_col(frame_col)).collect();

    let mut luma_row = vec![0u8; width as usize];
    let mut cb_row = vec![0u8; (width / 2) as usize];
    let mut cr_row = vec![0u8; (width / 2) as usize];

    for frame_row in 0..height as i32 {
        // 4:2:0: chroma is sampled on even rows and even columns only.
        let has_chroma = frame_row % 2 == 0;

        // Get the scene row this frame row falls into (0..20); in a 640x480 frame, e.g.
        // frame row 10 -> scene row 2, frame row 300 -> scene row 11.
        let scene_row = mapping.scene_row(frame_row);

        for frame_col in 0..width as usize {
            let scene_col = scene_cols_mapping[frame_col];

            let [luma, cb, cr] = palette[SCENE[scene_row * SCENE_WIDTH + scene_col] as usize];

            luma_row[frame_col] = luma;
            if has_chroma && frame_col % 2 == 0 {
                cb_row[frame_col / 2] = cb;
                cr_row[frame_col / 2] = cr;
            }
        }

        sink_y.write_all(&luma_row).map_err(|_| libc::EIO)?;
        if has_chroma {
            sink_u.write_all(&cb_row).map_err(|_| libc::EIO)?;
            sink_v.write_all(&cr_row).map_err(|_| libc::EIO)?;
        }
    }
    Ok(())
}

/// Per-frame processing settings.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Isp {
    pub gain: Gain,
}

impl Isp {
    /// Reads frame `iteration` out of `sensor` and writes it as YUV 4:2:0 planes.
    pub fn process(
        &self,
        sensor: &Sensor,
        iteration: u64,
        width: u32,
        height: u32,
        sink_y: &mut dyn Write,
        sink_u: &mut dyn Write,
        sink_v: &mut dyn Write,
    ) -> Result<(), i32> {
        let time_since_idx_ns = sensor.scene_time(iteration);

        let electrons = sensor.expose(time_since_idx_ns);
        let mut palette: Palette = [[0u8; 3]; NUM_MATERIALS];
        for (yuv, e) in palette.iter_mut().zip(electrons) {
            *yuv = electrons_to_yuv(e, self.gain);
        }

        let mapping = sensor.readout(width, height, time_since_idx_ns);
        raster_420(&mapping, &palette, width, height, sink_y, sink_u, sink_v)
    }
}
