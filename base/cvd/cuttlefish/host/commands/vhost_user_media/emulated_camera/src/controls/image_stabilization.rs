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

//! `V4L2_CID_IMAGE_STABILIZATION`: boolean cancelling the simulated hand-shake motion of the
//! camera in the emulated scene.

use v4l2r::bindings;

use crate::control::{Control, ControlDescriptor};
use crate::render_params::RenderParams;

/// Read-write boolean control, off by default.
#[derive(Debug, Default)]
pub struct ImageStabilizationControl {
    enabled: bool,
}

impl ImageStabilizationControl {
    pub fn new() -> Self {
        Self::default()
    }

    fn parse(value: i64) -> Result<bool, i32> {
        match value {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(libc::ERANGE),
        }
    }
}

impl Control for ImageStabilizationControl {
    fn id(&self) -> u32 {
        bindings::V4L2_CID_IMAGE_STABILIZATION
    }

    fn descriptor(&self) -> ControlDescriptor {
        ControlDescriptor {
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_BOOLEAN,
            name: "Image Stabilization",
            minimum: 0,
            maximum: 1,
            step: 1,
            default_value: 0,
            flags: 0,
        }
    }

    fn value(&self) -> i64 {
        self.enabled as i64
    }

    fn validate(&self, value: i64) -> Result<(), i32> {
        Self::parse(value).map(|_| ())
    }

    fn set(&mut self, value: i64) -> Result<bool, i32> {
        let enabled = Self::parse(value)?;
        let changed = self.enabled != enabled;
        self.enabled = enabled;
        Ok(changed)
    }

    fn apply(&self, params: &mut RenderParams) {
        params.image_stabilization = self.enabled;
    }
}
