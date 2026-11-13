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

//! Parameters consumed by the frame generators.
//!
//! [`RenderParams`] is the only thing the rendering pipeline knows about V4L2 controls. It is a
//! plain value with a default for every field, so a backend that does not expose a given control
//! simply renders with the default. Controls project their value into it through
//! [`crate::control::Control::apply`].

use crate::pattern::FramePattern;
use crate::pattern::julia_set::JuliaSet;
use crate::pattern::pulse::Pulse;
use crate::pattern::smpte::SmpteBars;
use crate::sensor::{ACTIVE_ARRAY_HEIGHT, ACTIVE_ARRAY_WIDTH};

/// Encapsulates the camera gain value.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Gain(i32);

impl Gain {
    pub const MIN: i32 = 100;
    pub const MAX: i32 = 1600;
    pub const DEFAULT: i32 = 100;

    pub fn new(value: i32) -> Result<Self, libc::c_int> {
        if value < Self::MIN || value > Self::MAX {
            Err(libc::ERANGE)
        } else {
            Ok(Gain(value))
        }
    }

    pub fn value(&self) -> i32 {
        self.0
    }
}

impl Default for Gain {
    fn default() -> Self {
        Gain(Self::DEFAULT)
    }
}

/// Test pattern selectable through `V4L2_CID_TEST_PATTERN`.
///
/// The discriminants double as the menu indices reported by `VIDIOC_QUERYMENU`, so they
/// must stay contiguous and start at [`TestPattern::MIN`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TestPattern {
    Pulse = 0,
    SmpteBars = 1,
    JuliaSet = 2,
}

impl TestPattern {
    /// Lowest menu index, reported as `minimum` for `V4L2_CID_TEST_PATTERN`.
    pub const MIN: i32 = TestPattern::Pulse as i32;
    /// Highest menu index, reported as `maximum` for `V4L2_CID_TEST_PATTERN`.
    pub const MAX: i32 = TestPattern::JuliaSet as i32;
    /// Pattern selected until the guest asks for something else.
    pub const DEFAULT: TestPattern = TestPattern::Pulse;

    /// Human readable name reported by `VIDIOC_QUERYMENU`.
    pub fn name(self) -> &'static str {
        match self {
            TestPattern::Pulse => "Pulse",
            TestPattern::SmpteBars => "SMPTE + Bouncing Box",
            TestPattern::JuliaSet => "Animated Julia Set",
        }
    }

    /// Frame generator backing this pattern.
    pub(crate) fn generator(self) -> &'static dyn FramePattern {
        match self {
            TestPattern::Pulse => &Pulse,
            TestPattern::SmpteBars => &SmpteBars,
            TestPattern::JuliaSet => &JuliaSet,
        }
    }
}

impl Default for TestPattern {
    fn default() -> Self {
        Self::DEFAULT
    }
}

impl TryFrom<i32> for TestPattern {
    /// Raw `errno` reported to the guest for an out-of-range menu index.
    type Error = i32;

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(TestPattern::Pulse),
            1 => Ok(TestPattern::SmpteBars),
            2 => Ok(TestPattern::JuliaSet),
            _ => Err(libc::ERANGE),
        }
    }
}

/// Window of the sensor active array, in active array pixels.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Crop {
    pub left: u32,
    pub top: u32,
    pub width: u32,
    pub height: u32,
}

impl Crop {
    /// Largest zoom factor.
    pub const MAX_ZOOM: u32 = 4;

    /// Smallest crop size: the active array divided by `MAX_ZOOM` on each side.
    pub const MIN_WIDTH: u32 = ACTIVE_ARRAY_WIDTH / Self::MAX_ZOOM;
    pub const MIN_HEIGHT: u32 = ACTIVE_ARRAY_HEIGHT / Self::MAX_ZOOM;
    /// Largest crop size: the whole active array.
    pub const MAX_WIDTH: u32 = ACTIVE_ARRAY_WIDTH;
    pub const MAX_HEIGHT: u32 = ACTIVE_ARRAY_HEIGHT;

    /// The whole active array.
    pub const FULL: Crop = Crop {
        left: 0,
        top: 0,
        width: Self::MAX_WIDTH,
        height: Self::MAX_HEIGHT,
    };

    /// Closest valid crop to the requested one: inside the 1280x960 active array and no
    /// smaller than the array divided by `MAX_ZOOM` (4) on each side, 320x240.
    /// E.g. a 600x100 crop at (1000, -50) sticks out at the top and the right and is too
    /// short; it becomes a 600x240 crop at (680, 0):
    ///
    /// ```text
    ///              requested                            result
    ///                     ┌─────────┐
    ///   ┌─────────────────┼────┐    │           ┌───────────┬─────────┐
    ///   │                 └────┼────┘           │           │         │
    ///   │    active array      │                │           └─────────┤
    ///   │                      │                │    active array     │
    ///   │                      │                │                     │
    ///   └──────────────────────┘                └─────────────────────┘
    /// ```
    pub fn new(left: i32, top: i32, width: u32, height: u32) -> Crop {
        // The size must be between the smallest and the largest crop size.
        let new_width = width.clamp(Self::MIN_WIDTH, Self::MAX_WIDTH);
        let new_height = height.clamp(Self::MIN_HEIGHT, Self::MAX_HEIGHT);
        // The position must keep the crop inside the active array.
        let new_left = left.clamp(0, (Self::MAX_WIDTH - new_width) as i32) as u32;
        let new_top = top.clamp(0, (Self::MAX_HEIGHT - new_height) as i32) as u32;
        Crop {
            left: new_left,
            top: new_top,
            width: new_width,
            height: new_height,
        }
    }
}

impl Default for Crop {
    fn default() -> Self {
        Self::FULL
    }
}

/// Everything the frame generators need to know about the current control state.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct RenderParams {
    /// Luma scaling, `Gain::MIN` is 1.0x.
    pub gain: Gain,
    /// Pattern rendered by [`crate::device::FrameSource::TestPatterns`].
    pub test_pattern: TestPattern,
    /// Window of the sensor active array rendered by
    /// [`crate::device::FrameSource::EmulatedScene`].
    pub crop: Crop,
}
