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

std::vector<cvd::Command> CrosvmManager::StartCommands(bool with_frontend) {
  cvd::Command crosvm_cmd(config_->crosvm_binary());
  crosvm_cmd.AddParameter("run");

  if (config_->gpu_mode() != vsoc::kGpuModeGuestSwiftshader) {
    crosvm_cmd.AddParameter("--gpu");
    if (config_->wayland_socket().size()) {
      crosvm_cmd.AddParameter("--wayland-sock=", config_->wayland_socket());
    }
    if (config_->x_display().size()) {
      crosvm_cmd.AddParameter("--x-display=", config_->x_display());
    }
  }
  if (!config_->ramdisk_image_path().empty()) {
    crosvm_cmd.AddParameter("--initrd=", config_->ramdisk_image_path());
  }
  crosvm_cmd.AddParameter("--null-audio");
  crosvm_cmd.AddParameter("--mem=", config_->memory_mb());
  crosvm_cmd.AddParameter("--cpus=", config_->cpus());
  crosvm_cmd.AddParameter("--params=", config_->kernel_cmdline_as_string());
  for (const auto& disk : config_->virtual_disk_paths()) {
    crosvm_cmd.AddParameter("--rwdisk=", disk);
  }
  crosvm_cmd.AddParameter("--socket=", GetControlSocketPath(config_));
  if (!config_->gsi_fstab_path().empty()) {
    crosvm_cmd.AddParameter("--android-fstab=", config_->gsi_fstab_path());
  }

  if (with_frontend) {
    crosvm_cmd.AddParameter("--single-touch=", config_->touch_socket_path(), ":",
                         config_->x_res(), ":", config_->y_res());
    crosvm_cmd.AddParameter("--keyboard=", config_->keyboard_socket_path());
  }

  AddTapFdParameter(&crosvm_cmd, config_->wifi_tap_name());
  AddTapFdParameter(&crosvm_cmd, config_->mobile_tap_name());

  // TODO remove this (use crosvm's seccomp files)
  crosvm_cmd.AddParameter("--disable-sandbox");

  if (config_->vsock_guest_cid() >= 2) {
    crosvm_cmd.AddParameter("--cid=", config_->vsock_guest_cid());
  }

  // TODO (138616941) re-enable the console on its own serial port

  // Redirect the first serial port with the kernel logs to the appropriate file
  crosvm_cmd.AddParameter("--serial=num=1,type=file,path=",
                       config_->kernel_log_pipe_name(),",console=true");
  // Use stdio for the second serial port, it contains the serial console.
  crosvm_cmd.AddParameter("--serial=num=2,type=stdout");

  // Redirect standard input and output to a couple of pipes for the console
  // forwarder host process to handle.
  cvd::SharedFD console_in_rd, console_in_wr, console_out_rd, console_out_wr;
  if (!cvd::SharedFD::Pipe(&console_in_rd, &console_in_wr) ||
      !cvd::SharedFD::Pipe(&console_out_rd, &console_out_wr)) {
    LOG(ERROR) << "Failed to create console pipes for crosvm: "
               << strerror(errno);
    return {};
  }

  crosvm_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdIn,
                           console_in_rd);
  crosvm_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut,
                           console_out_wr);
  cvd::Command console_cmd(config_->console_forwarder_binary());
  console_cmd.AddParameter("--console_in_fd=", console_in_wr);
  console_cmd.AddParameter("--console_out_fd=", console_out_rd);

  // This needs to be the last parameter
  crosvm_cmd.AddParameter(config_->GetKernelImageToUse());

  std::vector<cvd::Command> ret;
  ret.push_back(std::move(crosvm_cmd));
  ret.push_back(std::move(console_cmd));
  return ret;
}

bool CrosvmManager::Stop() {
  cvd::Command command(config_->crosvm_binary());
  command.AddParameter("stop");
  command.AddParameter(GetControlSocketPath(config_));

  auto process = command.Start();

  return process.Wait() == 0;
}

}  // namespace vm_manager
