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

//! The image sensor and its optics: turns the light of the [`crate::scene`] into
//! photo-electrons and maps every frame pixel to the scene tile it is looking at.

use crate::scene::{
    self, Lighting, Material, NUM_MATERIALS, SCENE_HEIGHT, SCENE_WIDTH, TIME_STEP, xyy_to_xyz,
};

// Handshake model constants. Frequencies in a nanosecond timebase.
const HORIZ_SHAKE_FREQ1: f32 = 2.0 * std::f32::consts::PI * 2.0 / 1e9; // 2 Hz
const HORIZ_SHAKE_FREQ2: f32 = 2.0 * std::f32::consts::PI * 13.0 / 1e9; // 13 Hz
const VERT_SHAKE_FREQ1: f32 = 2.0 * std::f32::consts::PI * 3.0 / 1e9; // 3 Hz
const VERT_SHAKE_FREQ2: f32 = 2.0 * std::f32::consts::PI * 11.0 / 1e9; // 11 Hz
const FREQ1_MAGNITUDE: f32 = 5.0;
const FREQ2_MAGNITUDE: f32 = 1.0;
/// Shake magnitude as a fraction of a scene tile.
const SHAKE_FRACTION: f32 = 0.03;

/// Aperture of the imaging lens (f-number).
const APERTURE: f32 = 2.8;

/// Sensor color filters in XYZ space, assumed to be the sRGB primaries (XYZ -> linear sRGB).
const FILTER_R: [f32; 3] = [3.2406, -1.5372, -0.4986];
const FILTER_G: [f32; 3] = [-0.9689, 1.8758, 0.0415];
const FILTER_B: [f32; 3] = [0.0557, -0.2040, 1.0570];

// Sensor model.
const SATURATION_VOLTAGE: f32 = 0.520;
/// Full-well capacity: electrons at which a pixel saturates.
pub const SATURATION_ELECTRONS: u32 = 2000;
const VOLTS_PER_LUX_SECOND: f32 = 0.100;
/// Sensor sensitivity, in electrons per lux-second.
const ELECTRONS_PER_LUX_SECOND: f32 =
    SATURATION_ELECTRONS as f32 / SATURATION_VOLTAGE * VOLTS_PER_LUX_SECOND;
/// Maximum RAW sample value; together with `SATURATION_ELECTRONS` defines the base gain.
pub const MAX_RAW_VALUE: u32 = 4000;

/// Photo-electrons (R, G, B) collected for each material, indexed by `Material as usize`.
pub type MaterialElectrons = [[u32; 3]; NUM_MATERIALS];

/// Maps frame rows and columns onto scene rows and columns (0..20).
pub struct Mapping {
    /// Frame pixels per scene row/column.
    map_div: i32,
    offset_x: i32,
    offset_y: i32,
    handshake_x: i32,
    handshake_y: i32,
}

impl Mapping {
    fn new(width: u32, height: u32, time_since_idx_ns: f32) -> Self {
        let (width, height) = (width as i32, height as i32);
        let map_div = if width > height {
            width / (SCENE_WIDTH as i32 + 1) + 1
        } else {
            height / (SCENE_HEIGHT as i32 + 1) + 1
        };
        let offset_x = (SCENE_WIDTH as i32 * map_div - width) / 2;
        let offset_y = (SCENE_HEIGHT as i32 * map_div - height) / 2;

        // Shake viewpoint; horizontal and vertical sinusoids at roughly human handshake
        // frequencies.
        let shake = |f1: f32, f2: f32| -> i32 {
            ((FREQ1_MAGNITUDE * (f1 * time_since_idx_ns).sin()
                + FREQ2_MAGNITUDE * (f2 * time_since_idx_ns).sin())
                * map_div as f32
                * SHAKE_FRACTION) as i32
        };
        Self {
            map_div,
            offset_x,
            offset_y,
            handshake_x: shake(HORIZ_SHAKE_FREQ1, HORIZ_SHAKE_FREQ2),
            handshake_y: shake(VERT_SHAKE_FREQ1, VERT_SHAKE_FREQ2),
        }
    }

    #[inline]
    pub fn scene_col(&self, frame_col: i32) -> usize {
        ((frame_col + self.offset_x + self.handshake_x) / self.map_div)
            .clamp(0, SCENE_WIDTH as i32 - 1) as usize
    }

    #[inline]
    pub fn scene_row(&self, frame_row: i32) -> usize {
        ((frame_row + self.offset_y + self.handshake_y) / self.map_div)
            .clamp(0, SCENE_HEIGHT as i32 - 1) as usize
    }
}

/// The image sensor looking at the scene.
pub struct Sensor {
    /// Hour of the day (24h clock) driving the illumination levels.
    pub hour: u32,
    /// Exposure time, in seconds.
    pub exposure_s: f32,
}

impl Default for Sensor {
    fn default() -> Self {
        Self {
            hour: Self::DEFAULT_HOUR,
            exposure_s: Self::DEFAULT_EXPOSURE_S,
        }
    }
}

impl Sensor {
    /// Noon, the brightest point of the day.
    pub const DEFAULT_HOUR: u32 = 12;
    /// Fixed exposure until auto-exposure exists. Chosen so that at noon the sunlit grass sits
    /// around half of sensor saturation at unity gain, with the sky brighter but not clipped.
    pub const DEFAULT_EXPOSURE_S: f32 = 0.0015;
    /// Frame interval used to turn the frame counter into scene time.
    const FRAME_INTERVAL_NS: u64 = 1_000_000_000 / 30;

    /// Nanoseconds since the start of the current `TIME_STEP`-hour illumination step at frame
    /// `iteration`.
    pub fn scene_time(&self, iteration: u64) -> f32 {
        let hour = self.hour % 24;
        const ONE_HOUR_NS: u64 = 1_000_000_000 * 60 * 60;
        let step_ns = ONE_HOUR_NS * TIME_STEP as u64;
        let time_ns = iteration.wrapping_mul(Self::FRAME_INTERVAL_NS) % step_ns;
        let time_since_idx_ns =
            ((hour - (hour / TIME_STEP) * TIME_STEP) as u64 * ONE_HOUR_NS + time_ns) % step_ns;
        time_since_idx_ns as f32
    }

    /// Photo-electrons (R, G, B) collected for each material at scene time
    /// `time_since_idx_ns`.
    pub fn expose(&self, time_since_idx_ns: f32) -> MaterialElectrons {
        let illum = scene::illumination(self.hour % 24, time_since_idx_ns);
        let lux_to_electrons = ELECTRONS_PER_LUX_SECOND * self.exposure_s / (APERTURE * APERTURE);
        let mut out = [[0u32; 3]; NUM_MATERIALS];
        for material in Material::ALL {
            let xyy = material.xyy();
            let mut mat_xyz = xyy_to_xyz([xyy[0], xyy[1]], xyy[2]);
            match material.lighting() {
                Lighting::Direct => {
                    for (m, l) in mat_xyz.iter_mut().zip(illum.direct_xyz) {
                        *m *= l;
                    }
                }
                Lighting::Shadowed => {
                    for (m, l) in mat_xyz.iter_mut().zip(illum.shade_xyz) {
                        *m *= l;
                    }
                }
                Lighting::SelfLit => {} // Emittance is already in lux.
            }

            let filtered = |f: [f32; 3]| -> u32 {
                let e =
                    (f[0] * mat_xyz[0] + f[1] * mat_xyz[1] + f[2] * mat_xyz[2]) * lux_to_electrons;
                // Negative responses are physically meaningless; clamp instead of wrapping.
                e.max(0.0) as u32
            };
            out[material as usize] = [filtered(FILTER_R), filtered(FILTER_G), filtered(FILTER_B)];
        }
        out
    }

    /// Maps each pixel of a `width`x`height` frame to the scene row/column it is looking at, at
    /// scene time `time_since_idx_ns`.
    pub fn readout(&self, width: u32, height: u32, time_since_idx_ns: f32) -> Mapping {
        Mapping::new(width, height, time_since_idx_ns)
    }
}
