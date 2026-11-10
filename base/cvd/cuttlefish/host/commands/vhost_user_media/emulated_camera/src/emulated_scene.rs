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

//! Minimal port of the Android emulated camera scene `https://cs.android.com/android/platform/superproject/main/+/main:hardware/google/camera/devices/EmulatedCamera/hwl/EmulatedScene.h`

use crate::render_params::{Gain, RenderParams};
use crate::pattern::FramePattern;
use std::io::Write;

pub struct EmulatedScene {
    /// Hour of the day (24h clock) driving the illumination levels.
    pub hour: u32,
}

impl Default for EmulatedScene {
    fn default() -> Self {
        Self { hour: Self::DEFAULT_HOUR }
    }
}

/// Surface materials that make up the tile map.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
enum Material {
    Grass,
    GrassShadow,
    Hill,
    Wall,
    Roof,
    Door,
    Chimney,
    Window,
    Sky,
}

/// How a material is lit.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Lighting {
    /// Reflects the direct (sun + moon + starlight) illumination.
    Direct,
    /// Reflects the shaded illumination.
    Shadowed,
    /// Emits a constant illuminance; unaffected by the time of day.
    SelfLit,
}

impl Material {
    /// Every material, for building per-frame lookup tables indexed by `Material as usize`.
    const ALL: [Material; 9] = [
        Material::Grass,
        Material::GrassShadow,
        Material::Hill,
        Material::Wall,
        Material::Roof,
        Material::Door,
        Material::Chimney,
        Material::Window,
        Material::Sky,
    ];

    /// Color in CIE xyY. For reflective materials Y is reflectance (1 = full); for self-lit
    /// materials it is the constant illuminance in lux.
    const fn xyy(self) -> [f32; 3] {
        match self {
            Material::Grass | Material::GrassShadow => [0.3688, 0.4501, 0.1329],
            Material::Hill => [0.3986, 0.5002, 0.4440],
            Material::Wall => [0.3262, 0.5040, 0.2297],
            Material::Roof => [0.4336, 0.3787, 0.1029],
            Material::Door => [0.3316, 0.2544, 0.0639],
            Material::Chimney => [0.3425, 0.3577, 0.0887],
            Material::Window => [INCANDESCENT_XY[0], INCANDESCENT_XY[1], LIVING_ROOM_ILLUM],
            Material::Sky => [
                NOON_SKY_XY[0],
                NOON_SKY_XY[1],
                DAYLIGHT_SHADE_ILLUM / DIRECT_SUN_ILLUM,
            ],
        }
    }

    const fn lighting(self) -> Lighting {
        match self {
            Material::Grass | Material::Sky => Lighting::Direct,
            Material::GrassShadow
            | Material::Hill
            | Material::Wall
            | Material::Roof
            | Material::Door
            | Material::Chimney => Lighting::Shadowed,
            Material::Window => Lighting::SelfLit,
        }
    }
}

const NUM_MATERIALS: usize = Material::ALL.len();

/// Light sources.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Luminary {
    Sun,
    Moon,
}

impl Luminary {
    /// Chromaticity (CIE xy) of the light cast when the luminary is high in the sky.
    const fn chromaticity(self) -> [f32; 2] {
        match self {
            Luminary::Sun => DIRECT_SUNLIGHT_XY,
            Luminary::Moon => MOONLIGHT_XY,
        }
    }

    /// Hour at which the luminary is overhead; before that, shaded materials still receive its
    /// direct light.
    const fn overhead_hour(self) -> u32 {
        match self {
            Luminary::Sun => SUN_OVERHEAD,
            Luminary::Moon => MOON_OVERHEAD,
        }
    }

    /// Illuminance through the day, in `TIME_STEP`-hour steps.
    const fn illuminance_table(self) -> &'static [f32; NUM_TIME_STEPS] {
        match self {
            Luminary::Sun => &SUNLIGHT,
            Luminary::Moon => &MOONLIGHT,
        }
    }
}

// Single-letter shortcuts for the scene definition.
use Material::Chimney as C;
use Material::Door as D;
use Material::Grass as G;
use Material::GrassShadow as S;
use Material::Hill as H;
use Material::Roof as R;
use Material::Sky as K;
use Material::Wall as W;
use Material::Window as I;

pub const SCENE_WIDTH: usize = 20;
pub const SCENE_HEIGHT: usize = 20;

#[rustfmt::skip]
const SCENE: [Material; SCENE_WIDTH * SCENE_HEIGHT] = [
    //      5         10        15        20
    K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K,
    K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K,
    K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K,
    K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K,
    K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, // 5
    K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K, K,
    K, K, K, K, K, K, K, K, H, H, H, H, H, H, H, H, H, H, H, H,
    K, K, K, K, K, K, K, K, H, H, H, H, H, H, H, C, C, H, H, H,
    K, K, K, K, K, K, H, H, H, H, H, H, H, H, H, C, C, H, H, H,
    H, K, K, K, K, K, H, R, R, R, R, R, R, R, R, R, R, R, R, H, // 10
    H, K, K, K, K, H, H, R, R, R, R, R, R, R, R, R, R, R, R, H,
    H, H, H, K, K, H, H, R, R, R, R, R, R, R, R, R, R, R, R, H,
    H, H, H, K, K, H, H, H, W, W, W, W, W, W, W, W, W, W, H, H,
    S, S, S, G, G, S, S, S, W, W, W, W, W, W, W, W, W, W, S, S,
    S, G, G, G, G, S, S, S, W, I, I, W, D, D, W, I, I, W, S, S, // 15
    G, G, G, G, G, G, S, S, W, I, I, W, D, D, W, I, I, W, S, S,
    G, G, G, G, G, G, G, G, W, W, W, W, D, D, W, W, W, W, G, G,
    G, G, G, G, G, G, G, G, W, W, W, W, D, D, W, W, W, W, G, G,
    G, G, G, G, G, G, G, G, S, S, S, S, S, S, S, S, S, S, G, G,
    G, G, G, G, G, G, G, G, S, S, S, S, S, S, S, S, S, S, G, G, // 20
    //      5         10        15        20
];

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

/// Sun/moon illuminance tables are sampled every `TIME_STEP` hours.
const TIME_STEP: u32 = 2;
const NUM_TIME_STEPS: usize = (24 / TIME_STEP) as usize;

// Illumination levels for various conditions, in lux.
const DIRECT_SUN_ILLUM: f32 = 100_000.0;
const SUNSET_ILLUM: f32 = 400.0;
const TWILIGHT_ILLUM: f32 = 4.0;
const FULL_MOON_ILLUM: f32 = 1.0;
const DAYLIGHT_SHADE_ILLUM: f32 = 20_000.0;
const CLEAR_NIGHT_ILLUM: f32 = 2e-3;
const LIVING_ROOM_ILLUM: f32 = 50.0;

/// Sun illumination through the day, in 2-hour steps. These don't match any real day anywhere.
const SUNLIGHT: [f32; NUM_TIME_STEPS] = [
    0.0, // 00:00
    0.0,
    0.0,
    TWILIGHT_ILLUM, // 06:00
    DIRECT_SUN_ILLUM,
    DIRECT_SUN_ILLUM,
    DIRECT_SUN_ILLUM, // 12:00
    DIRECT_SUN_ILLUM,
    DIRECT_SUN_ILLUM,
    SUNSET_ILLUM, // 18:00
    TWILIGHT_ILLUM,
    0.0,
];

/// Moon illumination through the day, in 2-hour steps.
const MOONLIGHT: [f32; NUM_TIME_STEPS] = [
    FULL_MOON_ILLUM, // 00:00
    FULL_MOON_ILLUM,
    0.0,
    0.0, // 06:00
    0.0,
    0.0,
    0.0, // 12:00
    0.0,
    0.0,
    0.0, // 18:00
    0.0,
    FULL_MOON_ILLUM,
];

const SUN_OVERHEAD: u32 = 12;
const MOON_OVERHEAD: u32 = 0;

// Chromaticity (CIE xy) of various illumination sources.
const INCANDESCENT_XY: [f32; 2] = [0.44757, 0.40745];
const DIRECT_SUNLIGHT_XY: [f32; 2] = [0.34842, 0.35161];
const DAYLIGHT_XY: [f32; 2] = [0.31271, 0.32902];
const NOON_SKY_XY: [f32; 2] = [0.346, 0.359];
const MOONLIGHT_XY: [f32; 2] = [0.34842, 0.35161];
const SUNSET_XY: [f32; 2] = [0.527, 0.413];

/// Sensor color filters in XYZ space, assumed to be the sRGB primaries (XYZ -> linear sRGB).
const FILTER_R: [f32; 3] = [3.2406, -1.5372, -0.4986];
const FILTER_G: [f32; 3] = [-0.9689, 1.8758, 0.0415];
const FILTER_B: [f32; 3] = [0.0557, -0.2040, 1.0570];

// Sensor model.
const SATURATION_VOLTAGE: f32 = 0.520;
const SATURATION_ELECTRONS: u32 = 2000;
const VOLTS_PER_LUX_SECOND: f32 = 0.100;
/// Sensor sensitivity, in electrons per lux-second.
const ELECTRONS_PER_LUX_SECOND: f32 =
    SATURATION_ELECTRONS as f32 / SATURATION_VOLTAGE * VOLTS_PER_LUX_SECOND;
/// Maximum RAW sample value; together with `SATURATION_ELECTRONS` defines the base gain.
const MAX_RAW_VALUE: u32 = 4000;
/// Fixed-point math with 6 bits of fractional precision.
const FIXED_BIT_PRECISION: i64 = 64;
/// Saturation point of the sensor after gain, in fixed point.
const SATURATION_POINT: i64 = FIXED_BIT_PRECISION * 255;

/// Illumination falling on the scene for a given time, split by direct and shaded light.
struct Illumination {
    /// Light reaching `Lighting::Direct` materials, in CIE 1931 XYZ.
    direct_xyz: [f32; 3],
    /// Light reaching `Lighting::Shadowed` materials, in CIE 1931 XYZ.
    shade_xyz: [f32; 3],
}

/// Converts a color from CIE 1931 xyY to CIE 1931 XYZ.
#[inline]
fn xyy_to_xyz(xy: [f32; 2], lum: f32) -> [f32; 3] {
    // X = Y / y * x, Y = Y, Z = Y / y * (1 - x - y)
    [lum / xy[1] * xy[0], lum, lum / xy[1] * (1.0 - xy[0] - xy[1])]
}

#[inline]
fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a * (1.0 - t) + b * t
}

#[inline]
fn add3(a: &mut [f32; 3], b: [f32; 3]) {
    a[0] += b[0];
    a[1] += b[1];
    a[2] += b[2];
}

/// Direct and shaded light falling on the scene at the given time of day.
fn illumination(hour: u32, time_since_idx_ns: f32) -> Illumination {
    const ONE_HOUR_NS: f32 = 1e9 * 60.0 * 60.0;
    let time_idx = (hour / TIME_STEP) as usize;
    let next_time_idx = (time_idx + 1) % NUM_TIME_STEPS;
    let time_frac = time_since_idx_ns / (ONE_HOUR_NS * TIME_STEP as f32);

    // Overall sunlight levels.
    let sunlight = Luminary::Sun.illuminance_table();
    let sun_lux = lerp(sunlight[time_idx], sunlight[next_time_idx], time_frac);
    let sun_shade_lux = sun_lux * (DAYLIGHT_SHADE_ILLUM / DIRECT_SUN_ILLUM);

    // Sun/shade chromaticity: reddish around sunrise/sunset, neutral otherwise.
    let sun_shade_xy_at = |idx: usize| -> ([f32; 2], [f32; 2]) {
        if sunlight[idx] == SUNSET_ILLUM || sunlight[idx] == TWILIGHT_ILLUM {
            (SUNSET_XY, SUNSET_XY)
        } else {
            (Luminary::Sun.chromaticity(), DAYLIGHT_XY)
        }
    };
    let (prev_sun_xy, prev_shade_xy) = sun_shade_xy_at(time_idx);
    let (next_sun_xy, next_shade_xy) = sun_shade_xy_at(next_time_idx);
    let sun_xy = [
        lerp(prev_sun_xy[0], next_sun_xy[0], time_frac),
        lerp(prev_sun_xy[1], next_sun_xy[1], time_frac),
    ];
    let shade_xy = [
        lerp(prev_shade_xy[0], next_shade_xy[0], time_frac),
        lerp(prev_shade_xy[1], next_shade_xy[1], time_frac),
    ];
    let sun_xyz = xyy_to_xyz(sun_xy, sun_lux);
    let sun_shade_xyz = xyy_to_xyz(shade_xy, sun_shade_lux);

    // Moonlight levels.
    let moonlight = Luminary::Moon.illuminance_table();
    let moon_xy = Luminary::Moon.chromaticity();
    let moon_lux = lerp(moonlight[time_idx], moonlight[next_time_idx], time_frac);
    let moon_shade_lux = moon_lux * (DAYLIGHT_SHADE_ILLUM / DIRECT_SUN_ILLUM);
    let moon_xyz = xyy_to_xyz(moon_xy, moon_lux);
    let moon_shade_xyz = xyy_to_xyz(moon_xy, moon_shade_lux);

    // Starlight level.
    let clear_night_xyz = xyy_to_xyz(moon_xy, CLEAR_NIGHT_ILLUM);

    // Direct light: sun + moon + stars.
    let mut direct_xyz = clear_night_xyz;
    add3(&mut direct_xyz, sun_xyz);
    add3(&mut direct_xyz, moon_xyz);

    // Shaded light: stars, plus full sun until it is overhead and shade light after, plus the
    // moon equivalent (the moon-up period covers the 23->0 transition, so shift by 12h).
    let mut shade_xyz = clear_night_xyz;
    add3(
        &mut shade_xyz,
        if hour < Luminary::Sun.overhead_hour() { sun_xyz } else { sun_shade_xyz },
    );
    let adj_hour = (hour + 12) % 24;
    let adj_moon_overhead = (Luminary::Moon.overhead_hour() + 12) % 24;
    add3(
        &mut shade_xyz,
        if adj_hour < adj_moon_overhead { moon_xyz } else { moon_shade_xyz },
    );

    Illumination {
        direct_xyz,
        shade_xyz,
    }
}

/// Photo-electrons (R, G, B) collected for each material under `illum`.
fn material_electrons(illum: &Illumination, exposure_s: f32) -> [[u32; 3]; NUM_MATERIALS] {
    let lux_to_electrons = ELECTRONS_PER_LUX_SECOND * exposure_s / (APERTURE * APERTURE);
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
            let e = (f[0] * mat_xyz[0] + f[1] * mat_xyz[1] + f[2] * mat_xyz[2]) * lux_to_electrons;
            // Negative responses are physically meaningless; clamp instead of wrapping.
            e.max(0.0) as u32
        };
        out[material as usize] = [filtered(FILTER_R), filtered(FILTER_G), filtered(FILTER_B)];
    }
    out
}

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

/// Converts electron counts to YCbCr using the JFIF RGB->YCbCr transform.
fn electrons_to_yuv(electrons: [u32; 3], gain: Gain) -> [u8; 3] {
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

/// Maps frame rows and columns onto scene rows and columns (0..20).
struct Mapping {
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
    fn scene_col(&self, frame_col: i32) -> usize {
        ((frame_col + self.offset_x + self.handshake_x) / self.map_div)
            .clamp(0, SCENE_WIDTH as i32 - 1) as usize
    }

    #[inline]
    fn scene_row(&self, frame_row: i32) -> usize {
        ((frame_row + self.offset_y + self.handshake_y) / self.map_div)
            .clamp(0, SCENE_HEIGHT as i32 - 1) as usize
    }
}

impl EmulatedScene {
    /// Noon, the brightest point of the day.
    pub const DEFAULT_HOUR: u32 = 12;
    /// Fixed exposure until auto-exposure exists. Chosen so that at noon the sunlit grass sits
    /// around half of sensor saturation at unity gain, with the sky brighter but not clipped.
    pub const DEFAULT_EXPOSURE_S: f32 = 0.0015;
    /// Frame interval used to turn the frame counter into scene time.
    const FRAME_INTERVAL_NS: u64 = 1_000_000_000 / 30;

    /// Per-material YCbCr for the given frame.
    fn palette(&self, iteration: u64, gain: Gain) -> ([[u8; 3]; NUM_MATERIALS], f32) {
        let hour = self.hour % 24;
        // Nanoseconds since the start of the current 2-hour illumination step.
        const ONE_HOUR_NS: u64 = 1_000_000_000 * 60 * 60;
        let step_ns = ONE_HOUR_NS * TIME_STEP as u64;
        let time_ns = iteration.wrapping_mul(Self::FRAME_INTERVAL_NS) % step_ns;
        let time_since_idx_ns =
            ((hour - (hour / TIME_STEP) * TIME_STEP) as u64 * ONE_HOUR_NS + time_ns) % step_ns;
        let time_since_idx_ns = time_since_idx_ns as f32;

        let illum = illumination(hour, time_since_idx_ns);
        let electrons = material_electrons(&illum, Self::DEFAULT_EXPOSURE_S);
        let mut palette = [[0u8; 3]; NUM_MATERIALS];
        for (yuv, e) in palette.iter_mut().zip(electrons) {
            *yuv = electrons_to_yuv(e, gain);
        }
        (palette, time_since_idx_ns)
    }
}

impl FramePattern for EmulatedScene {
    fn write(
        &self,
        iteration: u64,
        params: &RenderParams,
        width: u32,
        height: u32,
        sink_y: &mut dyn Write,
        sink_u: &mut dyn Write,
        sink_v: &mut dyn Write,
    ) -> Result<(), i32> {
        let (palette, time_since_idx_ns) = self.palette(iteration, params.gain);
        let mapping = Mapping::new(width, height, time_since_idx_ns);

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
}

