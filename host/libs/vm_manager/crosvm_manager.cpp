/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "host/libs/vm_manager/crosvm_manager.h"

#include <string>
#include <vector>

#include <glog/logging.h>

#include "common/libs/utils/network.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/disk_config.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace vm_manager {

namespace {

std::string GetControlSocketPath(const vsoc::CuttlefishConfig* config) {
  return config->PerInstancePath("crosvm_control.sock");
}

void AddTapFdParameter(cvd::Command* crosvm_cmd, const std::string& tap_name) {
  auto tap_fd = cvd::OpenTapInterface(tap_name);
  if (tap_fd->IsOpen()) {
    crosvm_cmd->AddParameter("--tap-fd=", tap_fd);
  } else {
    LOG(ERROR) << "Unable to connect to " << tap_name << ": "
               << tap_fd->StrError();
  }
}

}  // namespace

const std::string CrosvmManager::name() { return "crosvm"; }

bool CrosvmManager::ConfigureGpu(vsoc::CuttlefishConfig* config) {
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  if (config->gpu_mode() == vsoc::kGpuModeDrmVirgl) {
    config->add_kernel_cmdline("androidboot.hardware.gralloc=minigbm");
    config->add_kernel_cmdline("androidboot.hardware.hwcomposer=drm_minigbm");
    config->add_kernel_cmdline("androidboot.hardware.egl=mesa");
    return true;
  }
  if (config->gpu_mode() == vsoc::kGpuModeGuestSwiftshader) {
    config->add_kernel_cmdline(
        "androidboot.hardware.gralloc=cutf_ashmem");
    config->add_kernel_cmdline(
        "androidboot.hardware.hwcomposer=cutf_cvm_ashmem");
    config->add_kernel_cmdline("androidboot.hardware.egl=swiftshader");
    return true;
  }
  return false;
}

void CrosvmManager::ConfigureBootDevices(vsoc::CuttlefishConfig* config) {
  // PCI domain 0, bus 0, device 1, function 0
  // TODO There is no way to control this assignment with crosvm (yet)
  config->add_kernel_cmdline(
    "androidboot.boot_devices=pci0000:00/0000:00:01.0");
}

CrosvmManager::CrosvmManager(const vsoc::CuttlefishConfig* config)
    : VmManager(config) {}

cvd::Command CrosvmManager::StartCommand(bool with_frontend) {
  // Run crosvm directly instead of through a cf_crosvm.sh script. The kernel
  // logs are on crosvm's standard output, so we need to redirect it to the log
  // monitor socket, a helper script will print more than just the logs to
  // standard output.
  cvd::Command command(config_->crosvm_binary());
  command.AddParameter("run");

  if (config_->gpu_mode() != vsoc::kGpuModeGuestSwiftshader) {
    command.AddParameter("--gpu");
    command.AddParameter("--wayland-sock=", config_->wayland_socket());
  }
  if (!config_->ramdisk_image_path().empty()) {
    command.AddParameter("--initrd=", config_->ramdisk_image_path());
  }
  command.AddParameter("--null-audio");
  command.AddParameter("--mem=", config_->memory_mb());
  command.AddParameter("--cpus=", config_->cpus());
  command.AddParameter("--params=", config_->kernel_cmdline_as_string());
  if (config_->composite_disk_path().empty()) {
    if (config_->super_image_path().empty()) {
      command.AddParameter("--rwdisk=", config_->system_image_path());
    } else {
      command.AddParameter("--rwdisk=", config_->super_image_path());
    }
    command.AddParameter("--rwdisk=", config_->data_image_path());
    command.AddParameter("--rwdisk=", config_->cache_image_path());
    command.AddParameter("--rwdisk=", config_->metadata_image_path());
    if (config_->super_image_path().empty()) {
      command.AddParameter("--rwdisk=", config_->vendor_image_path());
      command.AddParameter("--rwdisk=", config_->product_image_path());
    }
  } else {
    if (should_create_composite_disk(*config_)) {
      create_composite_disk(*config_);
    }
    command.AddParameter("--rwdisk=", config_->composite_disk_path());
  }
  command.AddParameter("--socket=", GetControlSocketPath(config_));
  if (!config_->gsi_fstab_path().empty()) {
    command.AddParameter("--android-fstab=", config_->gsi_fstab_path());
  }

  if (with_frontend) {
    command.AddParameter("--single-touch=", config_->touch_socket_path(), ":",
                         config_->x_res(), ":", config_->y_res());
    command.AddParameter("--keyboard=", config_->keyboard_socket_path());
  }

  AddTapFdParameter(&command, config_->wifi_tap_name());
  AddTapFdParameter(&command, config_->mobile_tap_name());

  // TODO remove this (use crosvm's seccomp files)
  command.AddParameter("--disable-sandbox");

  if (config_->vsock_guest_cid() >= 2) {
    command.AddParameter("--cid=", config_->vsock_guest_cid());
  }

  command.AddParameter("--serial=num=1,type=file,path=",
                       config_->kernel_log_pipe_name());
  command.AddParameter("--serial=num=2,type=sink,console=true");

  auto dev_null = cvd::SharedFD::Open("/dev/null", O_RDONLY);
  if (dev_null->IsOpen()) {
    command.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdIn, dev_null);
  } else {
    LOG(ERROR) << "Unable to open /dev/null for stdin redirection";
  }

  // This needs to be the last parameter
  command.AddParameter(config_->GetKernelImageToUse());

  return command;
}

bool CrosvmManager::Stop() {
  cvd::Command command(config_->crosvm_binary());
  command.AddParameter("stop");
  command.AddParameter(GetControlSocketPath(config_));

  auto process = command.Start();

  return process.Wait() == 0;
}

}  // namespace vm_manager
