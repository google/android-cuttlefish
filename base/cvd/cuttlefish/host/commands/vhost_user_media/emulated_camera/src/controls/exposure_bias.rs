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

//! `V4L2_CID_AUTO_EXPOSURE_BIAS`: integer menu of exposure bias values.

use v4l2r::bindings;

use crate::control::{Control, ControlDescriptor};
use crate::render_params::{ExposureBias, RenderParams};

/// Read-write integer menu control backed by [`ExposureBias`].
///
/// Like any menu control its value is an index into the menu; `VIDIOC_QUERYMENU` maps the
/// index to the EV millis of the entry, `ExposureBias::MIN + (index * ExposureBias::STEP)`,
/// as the V4L2 API requires for this control:
///
/// ```text
///   index   EV millis   EV
///       0       -3000   -3.0
///       1       -2500   -2.5
///       6           0    0.0   (default)
///      11        2500    2.5
///      12        3000    3.0
/// ```
#[derive(Debug, Default)]
pub struct ExposureBiasControl {
    bias: ExposureBias,
}

impl ExposureBiasControl {
    /// Index of the last menu entry.
    const MAX_INDEX: i32 = (ExposureBias::MAX - ExposureBias::MIN) / ExposureBias::STEP;
    /// Index of the 0 EV entry.
    const DEFAULT_INDEX: i32 = (ExposureBias::DEFAULT - ExposureBias::MIN) / ExposureBias::STEP;

    pub fn new() -> Self {
        Self::default()
    }

    fn ev_millis(index: i64) -> Result<i32, libc::c_int> {
        let index = i32::try_from(index).map_err(|_| libc::ERANGE)?;
        if index < 0 || index > Self::MAX_INDEX {
            return Err(libc::ERANGE);
        }
        Ok(ExposureBias::MIN + index * ExposureBias::STEP)
    }

    fn parse(value: i64) -> Result<ExposureBias, i32> {
        ExposureBias::new(Self::ev_millis(value)?)
    }

    fn index(&self) -> i32 {
        (self.bias.ev_millis() - ExposureBias::MIN) / ExposureBias::STEP
    }
}

impl Control for ExposureBiasControl {
    fn id(&self) -> u32 {
        bindings::V4L2_CID_AUTO_EXPOSURE_BIAS
    }

    fn descriptor(&self) -> ControlDescriptor {
        ControlDescriptor {
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER_MENU,
            name: "Auto Exposure, Bias",
            minimum: 0,
            maximum: Self::MAX_INDEX as i64,
            step: 1,
            default_value: Self::DEFAULT_INDEX as i64,
            flags: 0,
        }
    }

    fn value(&self) -> i64 {
        self.index() as i64
    }

    fn validate(&self, value: i64) -> Result<(), i32> {
        Self::parse(value).map(|_| ())
    }

    fn set(&mut self, value: i64) -> Result<bool, i32> {
        let bias = Self::parse(value)?;
        let changed = self.bias != bias;
        self.bias = bias;
        Ok(changed)
    }

    fn menu_value(&self, index: u32) -> Option<i64> {
        Self::ev_millis(index as i64).ok().map(i64::from)
    }

    fn apply(&self, params: &mut RenderParams) {
        params.exposure_bias = self.bias;
    }
}
