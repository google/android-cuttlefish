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

//! `V4L2_CID_GAIN`: luma scaling applied by the frame generators.

use v4l2r::bindings;

use crate::control::{Control, ControlDescriptor};
use crate::render_params::{Gain, RenderParams};

/// Read-write integer control backed by [`Gain`].
#[derive(Debug, Default)]
pub struct GainControl {
    gain: Gain,
}

impl GainControl {
    pub fn new() -> Self {
        Self::default()
    }
}

impl Control for GainControl {
    fn id(&self) -> u32 {
        bindings::V4L2_CID_GAIN
    }

    fn descriptor(&self) -> ControlDescriptor {
        ControlDescriptor {
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER,
            name: "Gain",
            minimum: Gain::MIN as i64,
            maximum: Gain::MAX as i64,
            step: 1,
            default_value: Gain::DEFAULT as i64,
            flags: 0,
        }
    }

    fn value(&self) -> i64 {
        self.gain.value() as i64
    }

    fn validate(&self, value: i64) -> Result<(), i32> {
        let value = i32::try_from(value).map_err(|_| libc::ERANGE)?;
        Gain::new(value).map(|_| ())
    }

    fn set(&mut self, value: i64) -> Result<bool, i32> {
        let value = i32::try_from(value).map_err(|_| libc::ERANGE)?;
        let gain = Gain::new(value)?;
        let changed = self.gain != gain;
        self.gain = gain;
        Ok(changed)
    }

    fn apply(&self, params: &mut RenderParams) {
        params.gain = self.gain;
    }
}
