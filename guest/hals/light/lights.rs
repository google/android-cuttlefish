/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
//! This module implements the ILights AIDL interface.

use std::collections::HashMap;
use std::sync::Mutex;

use log::info;

use android_hardware_light::aidl::android::hardware::light::{
    HwLight::HwLight, HwLightState::HwLightState, ILights::ILights, LightType::LightType,
};

use binder::{ExceptionCode, Interface, Status};

struct Light {
    hw_light: HwLight,
    state: HwLightState,
}

const NUM_DEFAULT_LIGHTS: i32 = 3;

/// Defined so we can implement the ILights AIDL interface.
pub struct LightsService {
    lights: Mutex<HashMap<i32, Light>>,
}

impl Interface for LightsService {}

impl LightsService {
    fn new(hw_lights: impl IntoIterator<Item = HwLight>) -> Self {
        let mut lights_map = HashMap::new();

        for hw_light in hw_lights {
            lights_map.insert(hw_light.id, Light { hw_light, state: Default::default() });
        }

        Self { lights: Mutex::new(lights_map) }
    }
}

impl Default for LightsService {
    fn default() -> Self {
        let id_mapping_closure =
            |light_id| HwLight { id: light_id, ordinal: light_id, r#type: LightType::BACKLIGHT };

        Self::new((1..=NUM_DEFAULT_LIGHTS).map(id_mapping_closure))
    }
}

impl ILights for LightsService {
    fn setLightState(&self, id: i32, state: &HwLightState) -> binder::Result<()> {
        info!("Lights setting state for id={} to color {:x}", id, state.color);

        if let Some(light) = self.lights.lock().unwrap().get_mut(&id) {
            light.state = *state;
            Ok(())
        } else {
            Err(Status::new_exception(ExceptionCode::UNSUPPORTED_OPERATION, None))
        }
    }

    fn getLights(&self) -> binder::Result<Vec<HwLight>> {
        info!("Lights reporting supported lights");
        Ok(self.lights.lock().unwrap().values().map(|light| light.hw_light).collect())
    }
}
