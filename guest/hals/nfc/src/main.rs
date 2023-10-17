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

use android_hardware_nfc::aidl::android::hardware::nfc::INfc::{BnNfc, INfcAsyncServer};
use binder::{self, BinderFeatures, ProcessState};
use binder_tokio::TokioRuntime;
use clap::Parser;
use log::{error, info, LevelFilter};
use std::path::PathBuf;
use std::{panic, process};
use tokio::runtime::Runtime;

mod nci;
mod nfc;

use crate::nfc::NfcService;

const LOG_TAG: &str = "CfNfc";

#[derive(Parser)]
struct Cli {
    /// Virtio-console dev driver path
    #[arg(long)]
    virtio_dev_path: PathBuf,
}

fn main() {
    android_logger::init_once(
        android_logger::Config::default().with_tag(LOG_TAG).with_max_level(LevelFilter::Debug),
    );

    // Redirect panic messages to logcat.
    panic::set_hook(Box::new(|panic_info| {
        error!("{}", panic_info);
        process::exit(0); // Force panic in thread to quit.
    }));

    // Start binder thread pool with the minimum threads pool (= 1),
    // because NFC APEX is the only user of the NFC HAL.
    ProcessState::set_thread_pool_max_thread_count(0);
    ProcessState::start_thread_pool();

    // Prepare Tokio runtime with default (multi-threaded) configurations.
    // We'll spawn I/O threads in AIDL calls, so Runtime can't create with current thread
    // as other HALs do.
    let runtime = Runtime::new().expect("Failed to initialize Tokio runtime");

    // Initializes.
    let cli = Cli::parse();
    let nfc_service = runtime.block_on(NfcService::new(&cli.virtio_dev_path));
    let nfc_service_binder =
        BnNfc::new_async_binder(nfc_service, TokioRuntime(runtime), BinderFeatures::default());

    let service_name = format!("{}/default", NfcService::get_descriptor());
    info!("Starting {service_name} with {:?}", cli.virtio_dev_path);
    binder::add_service(&service_name, nfc_service_binder.as_binder())
        .expect("Failed to register service");

    // Wait for binder thread to be completed. Unexpected for HAL, though.
    ProcessState::join_thread_pool();

    info!("NFC HAL is shutting down");
}
