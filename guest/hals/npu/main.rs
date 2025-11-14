/*
 * Copyright (C) 2025 The Android Open Source Project
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
//! This implements the NPU Scheduling Service for Cuttlefish.

use android_hardware_npu::aidl::android::hardware::npu::IScheduling::{BnScheduling, IScheduling};
use anyhow::{Context, Result};
use binder::BinderFeatures;
use log::{error, LevelFilter};

use std::sync::Arc;

mod command_server;
mod commands;
mod scheduling;

use scheduling::SchedulingService;

const LOG_TAG: &str = "android.hardware.npu";

fn main() -> Result<()> {
    if !logger::init(
        logger::Config::default().with_tag_on_device(LOG_TAG).with_max_level(LevelFilter::Trace),
    ) {
        error!("Failed to start logger");
    }

    binder::ProcessState::set_thread_pool_max_thread_count(0);

    let npu_service = Arc::new(SchedulingService::new());

    command_server::start(npu_service.clone()).context("Failed to start the command server")?;

    let proxy = SchedulingService::create_service_proxy(npu_service);
    let npu_service_binder = BnScheduling::new_binder(proxy, BinderFeatures::default());
    let service_name = format!("{}/default", SchedulingService::get_descriptor());
    binder::add_service(&service_name, npu_service_binder.as_binder())
        .context("Failed to register service")?;
    binder::ProcessState::join_thread_pool();
    Ok(())
}
