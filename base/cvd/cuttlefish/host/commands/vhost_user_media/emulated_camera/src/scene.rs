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

//! The world in front of the camera: a tile map of materials and the light falling on it.
//!
//! Minimal port of the Android emulated camera scene `https://cs.android.com/android/platform/superproject/main/+/main:hardware/google/camera/devices/EmulatedCamera/hwl/EmulatedScene.h`

/// Surface materials that make up the tile map.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Material {
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
pub enum Lighting {
    /// Reflects the direct (sun + moon + starlight) illumination.
    Direct,
    /// Reflects the shaded illumination.
    Shadowed,
    /// Emits a constant illuminance; unaffected by the time of day.
    SelfLit,
}

impl Material {
    /// Every material, for building per-frame lookup tables indexed by `Material as usize`.
    pub const ALL: [Material; 9] = [
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
    pub const fn xyy(self) -> [f32; 3] {
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

    pub const fn lighting(self) -> Lighting {
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

pub const NUM_MATERIALS: usize = Material::ALL.len();

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
pub const SCENE: [Material; SCENE_WIDTH * SCENE_HEIGHT] = [
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

/// Sun/moon illuminance tables are sampled every `TIME_STEP` hours.
pub const TIME_STEP: u32 = 2;
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

/// Illumination falling on the scene for a given time, split by direct and shaded light.
pub struct Illumination {
    /// Light reaching `Lighting::Direct` materials, in CIE 1931 XYZ.
    pub direct_xyz: [f32; 3],
    /// Light reaching `Lighting::Shadowed` materials, in CIE 1931 XYZ.
    pub shade_xyz: [f32; 3],
}

/// Converts a color from CIE 1931 xyY to CIE 1931 XYZ.
#[inline]
pub fn xyy_to_xyz(xy: [f32; 2], lum: f32) -> [f32; 3] {
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
pub fn illumination(hour: u32, time_since_idx_ns: f32) -> Illumination {
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
