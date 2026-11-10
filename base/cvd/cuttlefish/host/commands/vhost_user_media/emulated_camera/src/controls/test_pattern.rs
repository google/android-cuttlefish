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

//! `V4L2_CID_TEST_PATTERN`: menu control selecting the generated test pattern.

use v4l2r::bindings;

use crate::control::{Control, ControlDescriptor};
use crate::render_params::{RenderParams, TestPattern};

/// Read-write menu control backed by [`TestPattern`].
#[derive(Debug, Default)]
pub struct TestPatternControl {
    pattern: TestPattern,
}

impl TestPatternControl {
    pub fn new() -> Self {
        Self::default()
    }

    fn parse(value: i64) -> Result<TestPattern, i32> {
        let value = i32::try_from(value).map_err(|_| libc::ERANGE)?;
        TestPattern::try_from(value)
    }
}

impl Control for TestPatternControl {
    fn id(&self) -> u32 {
        bindings::V4L2_CID_TEST_PATTERN
    }

    fn descriptor(&self) -> ControlDescriptor {
        ControlDescriptor {
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_MENU,
            name: "Test Pattern",
            minimum: TestPattern::MIN as i64,
            maximum: TestPattern::MAX as i64,
            step: 1,
            default_value: TestPattern::DEFAULT as i64,
            flags: 0,
        }
    }

    fn value(&self) -> i64 {
        self.pattern as i64
    }

    fn validate(&self, value: i64) -> Result<(), i32> {
        Self::parse(value).map(|_| ())
    }

    fn set(&mut self, value: i64) -> Result<bool, i32> {
        let pattern = Self::parse(value)?;
        let changed = self.pattern != pattern;
        self.pattern = pattern;
        Ok(changed)
    }

    fn menu_name(&self, index: u32) -> Option<&'static str> {
        // Menu indices are the `TestPattern` discriminants, so the enum decides the range.
        i32::try_from(index)
            .ok()
            .and_then(|index| TestPattern::try_from(index).ok())
            .map(TestPattern::name)
    }

    fn apply(&self, params: &mut RenderParams) {
        params.test_pattern = self.pattern;
    }
}
