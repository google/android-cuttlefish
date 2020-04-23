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

#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <android-base/logging.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace vm_manager {

namespace {

std::string GetControlSocketPath(const vsoc::CuttlefishConfig* config) {
  return config->ForDefaultInstance()
      .PerInstanceInternalPath("crosvm_control.sock");
}

cvd::SharedFD AddTapFdParameter(cvd::Command* crosvm_cmd,
                                const std::string& tap_name) {
  auto tap_fd = cvd::OpenTapInterface(tap_name);
  if (tap_fd->IsOpen()) {
    crosvm_cmd->AddParameter("--tap-fd=", tap_fd);
  } else {
    LOG(ERROR) << "Unable to connect to " << tap_name << ": "
               << tap_fd->StrError();
  }
  return tap_fd;
}

bool ReleaseDhcpLeases(const std::string& lease_path, cvd::SharedFD tap_fd) {
  auto lease_file_fd = cvd::SharedFD::Open(lease_path, O_RDONLY);
  if (!lease_file_fd->IsOpen()) {
    LOG(ERROR) << "Could not open leases file \"" << lease_path << '"';
    return false;
  }
  bool success = true;
  auto dhcp_leases = cvd::ParseDnsmasqLeases(lease_file_fd);
  for (auto& lease : dhcp_leases) {
    std::uint8_t dhcp_server_ip[] = {192, 168, 96, (std::uint8_t) (vsoc::ForCurrentInstance(1) * 4 - 3)};
    if (!cvd::ReleaseDhcp4(tap_fd, lease.mac_address, lease.ip_address, dhcp_server_ip)) {
      LOG(ERROR) << "Failed to release " << lease;
      success = false;
    } else {
      LOG(INFO) << "Successfully dropped " << lease;
    }
  }
  return success;
}

bool Stop() {
  auto config = vsoc::CuttlefishConfig::Get();
  cvd::Command command(config->crosvm_binary());
  command.AddParameter("stop");
  command.AddParameter(GetControlSocketPath(config));

  auto process = command.Start();

  return process.Wait() == 0;
}

}  // namespace

const std::string CrosvmManager::name() { return "crosvm"; }

std::vector<std::string> CrosvmManager::ConfigureGpu(const std::string& gpu_mode) {
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  if (gpu_mode == vsoc::kGpuModeGuestSwiftshader) {
    return {
        "androidboot.hardware.gralloc=cutf_ashmem",
        "androidboot.hardware.hwcomposer=cutf_hwc2",
        "androidboot.hardware.egl=swiftshader",
        "androidboot.hardware.vulkan=pastel",
    };
  }

  // Try to load the Nvidia modeset kernel module. Running Crosvm with Nvidia's EGL library on a
  // fresh machine after a boot will fail because the Nvidia EGL library will fork to run the
  // nvidia-modprobe command and the main Crosvm process will abort after receiving the exit signal
  // of the forked child which is interpreted as a failure.
  cvd::Command modprobe_cmd("/usr/bin/nvidia-modprobe");
  modprobe_cmd.AddParameter("--modeset");
  modprobe_cmd.Start().Wait();

  if (gpu_mode == vsoc::kGpuModeDrmVirgl) {
    return {
      "androidboot.hardware.gralloc=minigbm",
      "androidboot.hardware.hwcomposer=drm_minigbm",
      "androidboot.hardware.egl=mesa",
    };
  }
  if (gpu_mode == vsoc::kGpuModeGfxStream) {
    return {
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=drm_minigbm",
        "androidboot.hardware.egl=emulation",
        "androidboot.hardware.vulkan=ranchu",
        "androidboot.hardware.gltransport=virtio-gpu-pipe",
    };
  }
  return {};
}

std::vector<std::string> CrosvmManager::ConfigureBootDevices() {
  // PCI domain 0, bus 0, device 1, function 0
  // TODO There is no way to control this assignment with crosvm (yet)
  if (cvd::HostArch() == "x86_64") {
    return { "androidboot.boot_devices=pci0000:00/0000:00:01.0" };
  } else {
    return { "androidboot.boot_devices=10000.pci" };
  }
}

CrosvmManager::CrosvmManager(const vsoc::CuttlefishConfig* config)
    : VmManager(config) {}

std::vector<cvd::Command> CrosvmManager::StartCommands() {
  auto instance = config_->ForDefaultInstance();
  cvd::Command crosvm_cmd(config_->crosvm_binary(), [](cvd::Subprocess* proc) {
    auto stopped = Stop();
    if (stopped) {
      return true;
    }
    LOG(WARNING) << "Failed to stop VMM nicely, attempting to KILL";
    return KillSubprocess(proc);
  });
  crosvm_cmd.AddParameter("run");

  auto gpu_mode = config_->gpu_mode();

  if (gpu_mode == vsoc::kGpuModeDrmVirgl ||
      gpu_mode == vsoc::kGpuModeGfxStream) {
    crosvm_cmd.AddParameter(gpu_mode == vsoc::kGpuModeGfxStream ?
                                "--gpu=gfxstream," : "--gpu=",
                            "width=", config_->x_res(), ",",
                            "height=", config_->y_res(), ",",
                            "egl=true,surfaceless=true,glx=false,gles=true");
    crosvm_cmd.AddParameter("--wayland-sock=", instance.frames_socket_path());
  }
  if (!config_->final_ramdisk_path().empty()) {
    crosvm_cmd.AddParameter("--initrd=", config_->final_ramdisk_path());
  }
  crosvm_cmd.AddParameter("--null-audio");
  crosvm_cmd.AddParameter("--mem=", config_->memory_mb());
  crosvm_cmd.AddParameter("--cpus=", config_->cpus());
  crosvm_cmd.AddParameter("--params=", kernel_cmdline_);
  for (const auto& disk : instance.virtual_disk_paths()) {
    crosvm_cmd.AddParameter("--rwdisk=", disk);
  }
  crosvm_cmd.AddParameter("--socket=", GetControlSocketPath(config_));

  if (frontend_enabled_) {
    crosvm_cmd.AddParameter("--single-touch=", instance.touch_socket_path(),
                            ":", config_->x_res(), ":", config_->y_res());
    crosvm_cmd.AddParameter("--keyboard=", instance.keyboard_socket_path());
  }

  auto wifi_tap = AddTapFdParameter(&crosvm_cmd, instance.wifi_tap_name());
  AddTapFdParameter(&crosvm_cmd, instance.mobile_tap_name());

  crosvm_cmd.AddParameter("--rw-pmem-device=", instance.access_kregistry_path());

  if (config_->enable_sandbox()) {
    const bool seccomp_exists = cvd::DirectoryExists(config_->seccomp_policy_dir());
    const std::string& var_empty_dir = vsoc::kCrosvmVarEmptyDir;
    const bool var_empty_available = cvd::DirectoryExists(var_empty_dir);
    if (!var_empty_available || !seccomp_exists) {
      LOG(FATAL) << var_empty_dir << " is not an existing, empty directory."
                 << "seccomp-policy-dir, " << config_->seccomp_policy_dir()
                 << " does not exist " << std::endl;
      return {};
    }
    crosvm_cmd.AddParameter("--seccomp-policy-dir=", config_->seccomp_policy_dir());
  } else {
    crosvm_cmd.AddParameter("--disable-sandbox");
  }

  if (instance.vsock_guest_cid() >= 2) {
    crosvm_cmd.AddParameter("--cid=", instance.vsock_guest_cid());
  }

  // Redirect the first serial port with the kernel logs to the appropriate file
  crosvm_cmd.AddParameter("--serial=num=1,type=file,path=",
                          instance.kernel_log_pipe_name(), ",console=true");

  // Redirect standard input to a pipe for the console forwarder host process
  // to handle.
  cvd::SharedFD console_in_rd, console_in_wr;
  if (!cvd::SharedFD::Pipe(&console_in_rd, &console_in_wr)) {
    LOG(ERROR) << "Failed to create console pipe for crosvm's stdin: "
               << console_in_rd->StrError();
    return {};
  }
  auto console_pipe_name = instance.console_pipe_name();
  if (mkfifo(console_pipe_name.c_str(), 0660) != 0) {
    auto error = errno;
    LOG(ERROR) << "Failed to create console fifo for crosvm: "
               << strerror(error);
    return {};
  }

  // This fd will only be read from, but it's open with write access as well to
  // keep the pipe open in case the subprocesses exit.
  cvd::SharedFD console_out_rd =
      cvd::SharedFD::Open(console_pipe_name.c_str(), O_RDWR);
  if (!console_out_rd->IsOpen()) {
    LOG(ERROR) << "Failed to open console fifo for reads: "
               << console_out_rd->StrError();
    return {};
  }
  // stdin is the only currently supported way to write data to a serial port in
  // crosvm. A file (named pipe) is used here instead of stdout to ensure only
  // the serial port output is received by the console forwarder as crosvm may
  // print other messages to stdout.
  crosvm_cmd.AddParameter("--serial=num=2,type=file,path=", console_pipe_name,
                          ",stdin=true");

  crosvm_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdIn,
                           console_in_rd);
  cvd::Command console_cmd(config_->console_forwarder_binary());
  console_cmd.AddParameter("--console_in_fd=", console_in_wr);
  console_cmd.AddParameter("--console_out_fd=", console_out_rd);

  // This needs to be the last parameter
  crosvm_cmd.AddParameter(config_->GetKernelImageToUse());

  // TODO(schuffelen): QEMU also needs this and this is not the best place for
  // this code. Find a better place to put it.
  auto lease_file =
      vsoc::ForCurrentInstance("/var/run/cuttlefish-dnsmasq-cvd-wbr-")
      + ".leases";
  if (!ReleaseDhcpLeases(lease_file, wifi_tap)) {
    LOG(ERROR) << "Failed to release wifi DHCP leases. Connecting to the wifi "
               << "network may not work.";
  }

  std::vector<cvd::Command> ret;
  ret.push_back(std::move(crosvm_cmd));
  ret.push_back(std::move(console_cmd));
  return ret;
}

}  // namespace vm_manager
