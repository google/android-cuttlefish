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

//! `CID_LENS_FACING`: read-only, virtio-media specific control telling the guest camera HAL
//! which way the camera faces.

use std::str::FromStr;

use v4l2r::bindings;

use crate::control::{Control, ControlDescriptor};

/// Offset for virtio-media custom camera class control id values.
const CID_OFFSET: u32 = bindings::V4L2_CID_CAMERA_CLASS_BASE + 0x100;
/// Id of the lens facing control.
pub const CID_LENS_FACING: u32 = CID_OFFSET + 1;

/// https://developer.android.com/reference/android/hardware/camera2/CameraMetadata#LENS_FACING_FRONT
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LensFacing {
    Front = 0,
    Back = 1,
    External = 2,
}

impl FromStr for LensFacing {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "FRONT" => Ok(LensFacing::Front),
            "BACK" => Ok(LensFacing::Back),
            "EXTERNAL" => Ok(LensFacing::External),
            _ => Err(format!(
                "Invalid lens facing: {}. Expected FRONT, BACK, or EXTERNAL",
                s
            )),
        }
    }
}

/// Read-only integer control reporting a fixed [`LensFacing`].
#[derive(Debug)]
pub struct LensFacingControl {
    lens_facing: LensFacing,
}

impl LensFacingControl {
    pub fn new(lens_facing: LensFacing) -> Self {
        Self { lens_facing }
    }
}

impl Control for LensFacingControl {
    fn id(&self) -> u32 {
        CID_LENS_FACING
    }

    fn descriptor(&self) -> ControlDescriptor {
        ControlDescriptor {
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER,
            name: "LENS_FACING",
            minimum: LensFacing::Front as i64,
            maximum: LensFacing::External as i64,
            step: 1,
            // The value never changes, so it is its own default.
            default_value: self.lens_facing as i64,
            flags: bindings::V4L2_CTRL_FLAG_READ_ONLY,
        }
    }

    fn value(&self) -> i64 {
        self.lens_facing as i64
    }

    fn validate(&self, _value: i64) -> Result<(), i32> {
        Err(libc::EACCES)
    }

    fn set(&mut self, _value: i64) -> Result<bool, i32> {
        Err(libc::EACCES)
    }
}
