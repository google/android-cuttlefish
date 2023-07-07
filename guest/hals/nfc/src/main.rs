// Copyright 2023, The Android Open Source Project
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

//! Cuttlefish's NFC HAL.

use android_hardware_nfc::aidl::android::hardware::nfc::INfc::{BnNfc, INfc};
use binder::{self, BinderFeatures, ProcessState};
use log::{error, info, LevelFilter};
use std::panic;

mod nfc;

use crate::nfc::NfcService;

const LOG_TAG: &str = "CfNfc";

fn main() {
    android_logger::init_once(
        android_logger::Config::default().with_tag(LOG_TAG).with_max_level(LevelFilter::Info),
    );

    // Redirect panic messages to logcat.
    panic::set_hook(Box::new(|panic_info| {
        error!("{}", panic_info);
    }));

    // Start binder thread pool with default number of threads pool (15)
    ProcessState::start_thread_pool();

    let nfc_service = NfcService::default();
    let nfc_service_binder = BnNfc::new_binder(nfc_service, BinderFeatures::default());

    let service_name = format!("{}/default", NfcService::get_descriptor());
    info!("Starting {service_name}");
    binder::add_service(&service_name, nfc_service_binder.as_binder())
        .expect("Failed to register service");

    ProcessState::join_thread_pool()
}
