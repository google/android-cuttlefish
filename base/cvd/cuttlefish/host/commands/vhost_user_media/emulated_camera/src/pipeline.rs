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

//! The imaging pipeline: [`crate::scene`] -> [`crate::sensor`] -> [`crate::isp`] -> frame.

use std::io::Write;

use crate::isp::Isp;
use crate::pattern::FramePattern;
use crate::render_params::RenderParams;
use crate::sensor::Sensor;

/// Renders the emulated scene as seen through the emulated sensor and ISP.
#[derive(Default)]
pub struct Pipeline {
    pub sensor: Sensor,
}

impl FramePattern for Pipeline {
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
        let isp = Isp { gain: params.gain };
        isp.process(&self.sensor, iteration, width, height, sink_y, sink_u, sink_v)
    }
}
