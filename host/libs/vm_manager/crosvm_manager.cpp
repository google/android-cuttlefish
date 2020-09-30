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
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {
namespace vm_manager {

namespace {

std::string GetControlSocketPath(const cuttlefish::CuttlefishConfig& config) {
  return config.ForDefaultInstance()
      .PerInstanceInternalPath("crosvm_control.sock");
}

cuttlefish::SharedFD AddTapFdParameter(cuttlefish::Command* crosvm_cmd,
                                const std::string& tap_name) {
  auto tap_fd = cuttlefish::OpenTapInterface(tap_name);
  if (tap_fd->IsOpen()) {
    crosvm_cmd->AddParameter("--tap-fd=", tap_fd);
  } else {
    LOG(ERROR) << "Unable to connect to " << tap_name << ": "
               << tap_fd->StrError();
  }
  return tap_fd;
}

bool ReleaseDhcpLeases(const std::string& lease_path, cuttlefish::SharedFD tap_fd) {
  auto lease_file_fd = cuttlefish::SharedFD::Open(lease_path, O_RDONLY);
  if (!lease_file_fd->IsOpen()) {
    LOG(ERROR) << "Could not open leases file \"" << lease_path << '"';
    return false;
  }
  bool success = true;
  auto dhcp_leases = cuttlefish::ParseDnsmasqLeases(lease_file_fd);
  for (auto& lease : dhcp_leases) {
    std::uint8_t dhcp_server_ip[] = {192, 168, 96, (std::uint8_t) (cuttlefish::ForCurrentInstance(1) * 4 - 3)};
    if (!cuttlefish::ReleaseDhcp4(tap_fd, lease.mac_address, lease.ip_address, dhcp_server_ip)) {
      LOG(ERROR) << "Failed to release " << lease;
      success = false;
    } else {
      LOG(INFO) << "Successfully dropped " << lease;
    }
  }
  return success;
}

bool Stop() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  cuttlefish::Command command(config->crosvm_binary());
  command.AddParameter("stop");
  command.AddParameter(GetControlSocketPath(*config));

  auto process = command.Start();

  return process.Wait() == 0;
}

}  // namespace

/* static */ std::string CrosvmManager::name() { return "crosvm"; }

bool CrosvmManager::IsSupported() {
  return HostSupportsQemuCli();
}

std::vector<std::string> CrosvmManager::ConfigureGpuMode(
    const std::string& gpu_mode) {
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  if (gpu_mode == cuttlefish::kGpuModeGuestSwiftshader) {
    return {
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=cutf",
        "androidboot.hardware.egl=swiftshader",
        "androidboot.hardware.vulkan=pastel",
    };
  }

  if (gpu_mode == cuttlefish::kGpuModeDrmVirgl) {
    return {
      "androidboot.hardware.gralloc=minigbm",
      "androidboot.hardware.hwcomposer=drm_minigbm",
      "androidboot.hardware.egl=mesa",
    };
  }
  if (gpu_mode == cuttlefish::kGpuModeGfxStream) {
    return {
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=drm_minigbm",
        "androidboot.hardware.egl=emulation",
        "androidboot.hardware.vulkan=ranchu",
        "androidboot.hardware.gltransport=virtio-gpu-asg",
    };
  }
  return {};
}

std::vector<std::string> CrosvmManager::ConfigureBootDevices() {
  // TODO There is no way to control this assignment with crosvm (yet)
  if (cuttlefish::HostArch() == "x86_64") {
    // PCI domain 0, bus 0, device 4, function 0
    return { "androidboot.boot_devices=pci0000:00/0000:00:04.0" };
  } else {
    return { "androidboot.boot_devices=10000.pci" };
  }
}

std::vector<cuttlefish::Command> CrosvmManager::StartCommands(
    const CuttlefishConfig& config,
    bool with_frontend,
    const std::string& kernel_cmdline) {
  auto instance = config.ForDefaultInstance();
  cuttlefish::Command crosvm_cmd(config.crosvm_binary(), [](cuttlefish::Subprocess* proc) {
    auto stopped = Stop();
    if (stopped) {
      return true;
    }
    LOG(WARNING) << "Failed to stop VMM nicely, attempting to KILL";
    return KillSubprocess(proc);
  });
  crosvm_cmd.AddParameter("run");

  auto gpu_mode = config.gpu_mode();

  if (gpu_mode == cuttlefish::kGpuModeGuestSwiftshader) {
    crosvm_cmd.AddParameter("--gpu=2D,",
                            "width=", config.x_res(), ",",
                            "height=", config.y_res());
  } else if (gpu_mode == cuttlefish::kGpuModeDrmVirgl ||
             gpu_mode == cuttlefish::kGpuModeGfxStream) {
    crosvm_cmd.AddParameter(gpu_mode == cuttlefish::kGpuModeGfxStream ?
                                "--gpu=gfxstream," : "--gpu=",
                            "width=", config.x_res(), ",",
                            "height=", config.y_res(), ",",
                            "egl=true,surfaceless=true,glx=false,gles=true");
    crosvm_cmd.AddParameter("--wayland-sock=", instance.frames_socket_path());
  }
  if (!config.final_ramdisk_path().empty()) {
    crosvm_cmd.AddParameter("--initrd=", config.final_ramdisk_path());
  }
  // crosvm_cmd.AddParameter("--null-audio");
  crosvm_cmd.AddParameter("--mem=", config.memory_mb());
  crosvm_cmd.AddParameter("--cpus=", config.cpus());
  crosvm_cmd.AddParameter("--params=", kernel_cmdline);
  for (const auto& disk : instance.virtual_disk_paths()) {
    crosvm_cmd.AddParameter("--rwdisk=", disk);
  }
  crosvm_cmd.AddParameter("--socket=", GetControlSocketPath(config));

  if (with_frontend) {
    crosvm_cmd.AddParameter("--single-touch=", instance.touch_socket_path(),
                            ":", config.x_res(), ":", config.y_res());
    crosvm_cmd.AddParameter("--keyboard=", instance.keyboard_socket_path());
  }

  auto wifi_tap = AddTapFdParameter(&crosvm_cmd, instance.wifi_tap_name());
  AddTapFdParameter(&crosvm_cmd, instance.mobile_tap_name());

  crosvm_cmd.AddParameter("--rw-pmem-device=", instance.access_kregistry_path());
  crosvm_cmd.AddParameter("--pstore=path=", instance.pstore_path(), ",size=",
                          cuttlefish::FileSize(instance.pstore_path()));

  if (config.enable_sandbox()) {
    const bool seccomp_exists = cuttlefish::DirectoryExists(config.seccomp_policy_dir());
    const std::string& var_empty_dir = cuttlefish::kCrosvmVarEmptyDir;
    const bool var_empty_available = cuttlefish::DirectoryExists(var_empty_dir);
    if (!var_empty_available || !seccomp_exists) {
      LOG(FATAL) << var_empty_dir << " is not an existing, empty directory."
                 << "seccomp-policy-dir, " << config.seccomp_policy_dir()
                 << " does not exist " << std::endl;
      return {};
    }
    crosvm_cmd.AddParameter("--seccomp-policy-dir=", config.seccomp_policy_dir());
  } else {
    crosvm_cmd.AddParameter("--disable-sandbox");
  }

  if (instance.vsock_guest_cid() >= 2) {
    crosvm_cmd.AddParameter("--cid=", instance.vsock_guest_cid());
  }

  // Use an 8250 UART (ISA or platform device) for earlycon, as the
  // virtio-console driver may not be available for early messages
  // In kgdb mode, earlycon is an interactive console, and so early
  // dmesg will go there instead of the kernel.log
  if (!(config.console() && (config.use_bootloader() || config.kgdb()))) {
    crosvm_cmd.AddParameter("--serial=hardware=serial,num=1,type=file,path=",
                            instance.kernel_log_pipe_name(), ",earlycon=true");
  }

  // Use a virtio-console instance for the main kernel console. All
  // messages will switch from earlycon to virtio-console after the driver
  // is loaded, and crosvm will append to the kernel log automatically
  crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=1,type=file,path=",
                          instance.kernel_log_pipe_name(), ",console=true");

  if (config.console()) {
    // stdin is the only currently supported way to write data to a serial port in
    // crosvm. A file (named pipe) is used here instead of stdout to ensure only
    // the serial port output is received by the console forwarder as crosvm may
    // print other messages to stdout.
    if (config.kgdb() || config.use_bootloader()) {
      crosvm_cmd.AddParameter("--serial=hardware=serial,num=1,type=file,path=",
                              instance.console_out_pipe_name(), ",input=",
                              instance.console_in_pipe_name(), ",earlycon=true");
      // In kgdb mode, we have the interactive console on ttyS0 (both Android's
      // console and kdb), so we can disable the virtio-console port usually
      // allocated to Android's serial console, and redirect it to a sink. This
      // ensures that that the PCI device assignments (and thus sepolicy) don't
      // have to change
      crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=2,type=sink");
    } else {
      crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=2,type=file,path=",
                              instance.console_out_pipe_name(), ",input=",
                              instance.console_in_pipe_name());
    }
  } else {
    // as above, create a fake virtio-console 'sink' port when the serial
    // console is disabled, so the PCI device ID assignments don't move
    // around
    crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=2,type=sink");
  }

  if (config.enable_gnss_grpc_proxy()) {
    crosvm_cmd.AddParameter("--serial=hardware=serial,num=2,type=file,path=",
                            instance.gnss_out_pipe_name(), ",input=",
                            instance.gnss_in_pipe_name());
  }

  cuttlefish::SharedFD log_out_rd, log_out_wr;
  if (!cuttlefish::SharedFD::Pipe(&log_out_rd, &log_out_wr)) {
    LOG(ERROR) << "Failed to create log pipe for crosvm's stdout/stderr: "
               << log_out_rd->StrError();
    return {};
  }
  crosvm_cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdOut,
                           log_out_wr);
  crosvm_cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdErr,
                           log_out_wr);

  cuttlefish::Command log_tee_cmd(cuttlefish::DefaultHostArtifactsPath("bin/log_tee"));
  log_tee_cmd.AddParameter("--process_name=crosvm");
  log_tee_cmd.AddParameter("--log_fd_in=", log_out_rd);

  // Serial port for logcat, redirected to a pipe
  crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=3,type=file,path=",
                          instance.logcat_pipe_name());

  // TODO(b/162071003): virtiofs crashes without sandboxing, this should be fixed
  if (config.enable_sandbox()) {
    // Set up directory shared with virtiofs
    crosvm_cmd.AddParameter("--shared-dir=", instance.PerInstancePath(cuttlefish::kSharedDirName),
                            ":shared:type=fs");
  }

  // This needs to be the last parameter
  if (config.use_bootloader()) {
    crosvm_cmd.AddParameter("--bios=", config.bootloader());
  } else {
    crosvm_cmd.AddParameter(config.GetKernelImageToUse());
  }

  // Only run the leases workaround if we are not using the new network
  // bridge architecture - in that case, we have a wider DHCP address
  // space and stale leases should be much less of an issue
  if (!cuttlefish::FileExists("/var/run/cuttlefish-dnsmasq-cvd-wbr.leases")) {
    // TODO(schuffelen): QEMU also needs this and this is not the best place for
    // this code. Find a better place to put it.
    auto lease_file =
        cuttlefish::ForCurrentInstance("/var/run/cuttlefish-dnsmasq-cvd-wbr-")
        + ".leases";
    if (!ReleaseDhcpLeases(lease_file, wifi_tap)) {
      LOG(ERROR) << "Failed to release wifi DHCP leases. Connecting to the wifi "
                 << "network may not work.";
    }
  }

  std::vector<cuttlefish::Command> ret;
  ret.push_back(std::move(crosvm_cmd));
  ret.push_back(std::move(log_tee_cmd));
  return ret;
}

} // namespace vm_manager
} // namespace cuttlefish

