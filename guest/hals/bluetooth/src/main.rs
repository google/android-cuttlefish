// Copyright 2024, The Android Open Source Project
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

//! Declared HAL services
//!  - android.hardware.bluetooth.IBluetoothHci/default

#![allow(unused_imports)]

use android_hardware_bluetooth::aidl::android::hardware::bluetooth::IBluetoothHci::BnBluetoothHci;
use binder::{self, BinderFeatures, ProcessState};
use log::{error, info};

mod hci;

#[derive(argh::FromArgs, Debug)]
/// Bluetooth HAL service.
struct Opt {
    #[argh(option, default = "String::from(\"/dev/hvc5\")")]
    /// select the HCI serial device.
    serial: String,
}

fn main() {
    let opt: Opt = argh::from_env();

    android_logger::init_once(
        android_logger::Config::default()
            .with_tag("bluetooth-cf")
            .with_max_level(log::LevelFilter::Debug),
    );

    // Redirect panic messages to logcat.
    std::panic::set_hook(Box::new(|message| {
        error!("{}", message);
        std::process::exit(-1);
    }));

    // Start binder thread pool with the minimum threads pool (= 1),
    // because Bluetooth APEX is the only user of the Bluetooth Audio HAL.
    ProcessState::set_thread_pool_max_thread_count(0);
    ProcessState::start_thread_pool();

    let hci_binder =
        BnBluetoothHci::new_binder(hci::BluetoothHci::new(&opt.serial), BinderFeatures::default());

    info!("Starting ..IBluetoothHci/default");
    binder::add_service("android.hardware.bluetooth.IBluetoothHci/default", hci_binder.as_binder())
        .expect("Failed to register IBluetoothHci/default service");

    ProcessState::join_thread_pool();
    info!("The Bluetooth HAL is shutting down");
}
