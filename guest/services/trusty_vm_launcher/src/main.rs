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

//! A client for early boot VM running trusty.

use android_system_virtualizationservice::aidl::android::system::virtualizationservice::{
    IVirtualizationService::IVirtualizationService, VirtualMachineConfig::VirtualMachineConfig,
    VirtualMachineRawConfig::VirtualMachineRawConfig,
};
use android_system_virtualizationservice::binder::{ParcelFileDescriptor, Strong};
use anyhow::{Context, Result};
use std::fs::File;
use vmclient::VmInstance;

const KERNEL_PATH: &str = "/system_ext/etc/vm/trusty_vm/lk_trusty.elf";

fn get_service() -> Result<Strong<dyn IVirtualizationService>> {
    let virtmgr = vmclient::VirtualizationService::new_early()
        .context("Failed to spawn VirtualizationService")?;
    virtmgr.connect().context("Failed to connect to VirtualizationService")
}

fn main() -> Result<()> {
    let service = get_service()?;

    let kernel =
        File::open(KERNEL_PATH).with_context(|| format!("Failed to open {KERNEL_PATH}"))?;

    let vm_config = VirtualMachineConfig::RawConfig(VirtualMachineRawConfig {
        name: "trusty_vm_launcher".to_owned(),
        kernel: Some(ParcelFileDescriptor::new(kernel)),
        protectedVm: false,
        memoryMib: 128,
        platformVersion: "~1.0".to_owned(),
        // TODO: add instanceId
        ..Default::default()
    });

    println!("creating VM");
    let vm = VmInstance::create(
        service.as_ref(),
        &vm_config,
        // console_in, console_out, and log will be redirected to the kernel log by virtmgr
        None, // console_in
        None, // console_out
        None, // log
        None, // dump_dt
        None, // callback
    )
    .context("Failed to create VM")?;
    vm.start().context("Failed to start VM")?;

    println!("started trusty_vm_launcher VM");
    let death_reason = vm.wait_for_death();
    eprintln!("trusty_vm_launcher ended: {:?}", death_reason);

    // TODO(b/331320802): we may want to use android logger instead of stdio_to_kmsg?

    Ok(())
}
