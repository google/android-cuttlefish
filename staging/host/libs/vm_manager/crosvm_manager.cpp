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

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vulkan/vulkan.h>

#include <cassert>
#include <string>
#include <vector>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/crosvm_builder.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {
namespace vm_manager {

namespace {

std::string GetControlSocketPath(
    const CuttlefishConfig::InstanceSpecific& instance,
    const std::string& socket_name) {
  return instance.PerInstanceInternalPath(socket_name.c_str());
}

bool ReleaseDhcpLeases(const std::string& lease_path, SharedFD tap_fd) {
  auto lease_file_fd = SharedFD::Open(lease_path, O_RDONLY);
  if (!lease_file_fd->IsOpen()) {
    LOG(ERROR) << "Could not open leases file \"" << lease_path << '"';
    return false;
  }
  bool success = true;
  auto dhcp_leases = ParseDnsmasqLeases(lease_file_fd);
  for (auto& lease : dhcp_leases) {
    std::uint8_t dhcp_server_ip[] = {192, 168, 96, (std::uint8_t) (ForCurrentInstance(1) * 4 - 3)};
    if (!ReleaseDhcp4(tap_fd, lease.mac_address, lease.ip_address, dhcp_server_ip)) {
      LOG(ERROR) << "Failed to release " << lease;
      success = false;
    } else {
      LOG(INFO) << "Successfully dropped " << lease;
    }
  }
  return success;
}

}  // namespace

bool CrosvmManager::IsSupported() {
#ifdef __ANDROID__
  return true;
#else
  return HostSupportsQemuCli();
#endif
}

std::vector<std::string> CrosvmManager::ConfigureGpuMode(
    const std::string& gpu_mode) {
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  if (gpu_mode == kGpuModeGuestSwiftshader) {
    return {
        "androidboot.cpuvulkan.version=" + std::to_string(VK_API_VERSION_1_2),
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=ranchu",
        "androidboot.hardware.egl=angle",
        "androidboot.hardware.vulkan=pastel",
    };
  }

  if (gpu_mode == kGpuModeDrmVirgl) {
    return {
      "androidboot.cpuvulkan.version=0",
      "androidboot.hardware.gralloc=minigbm",
      "androidboot.hardware.hwcomposer=drm_minigbm",
      "androidboot.hardware.egl=mesa",
    };
  }
  if (gpu_mode == kGpuModeGfxStream) {
    return {
        "androidboot.cpuvulkan.version=0",
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=ranchu",
        "androidboot.hardware.egl=emulation",
        "androidboot.hardware.vulkan=ranchu",
        "androidboot.hardware.gltransport=virtio-gpu-asg",
    };
  }
  return {};
}

std::string CrosvmManager::ConfigureBootDevices(int num_disks) {
  // TODO There is no way to control this assignment with crosvm (yet)
  if (HostArch() == Arch::X86_64) {
    // crosvm has an additional PCI device for an ISA bridge
    return ConfigureMultipleBootDevices("pci0000:00/0000:00:", 1, num_disks);
  } else {
    // On ARM64 crosvm, block devices are on their own bridge, so we don't
    // need to calculate it, and the path is always the same
    return "androidboot.boot_devices=10000.pci";
  }
}

constexpr auto crosvm_socket = "crosvm_control.sock";
constexpr auto crosvm_for_ap_socket = "crosvm_for_ap_control.sock";

std::vector<Command> CrosvmManager::StartCommands(
    const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  CrosvmBuilder crosvm_cmd;
  crosvm_cmd.SetBinary(config.crosvm_binary());
  crosvm_cmd.AddControlSocket(GetControlSocketPath(instance, crosvm_socket));

  bool use_ap_instance =
      !config.ap_rootfs_image().empty() && !config.ap_kernel_image().empty();

  CrosvmBuilder ap_cmd;
  ap_cmd.SetBinary(config.crosvm_binary());
  ap_cmd.AddControlSocket(GetControlSocketPath(instance, crosvm_for_ap_socket));

  if (!config.smt()) {
    crosvm_cmd.Cmd().AddParameter("--no-smt");
  }

  if (config.vhost_net()) {
    crosvm_cmd.Cmd().AddParameter("--vhost-net");
  }

  if (!config.vhost_user_mac80211_hwsim().empty()) {
    crosvm_cmd.Cmd().AddParameter("--vhost-user-mac80211-hwsim=",
                                  config.vhost_user_mac80211_hwsim());
    ap_cmd.Cmd().AddParameter("--vhost-user-mac80211-hwsim=",
                              config.vhost_user_mac80211_hwsim());
  }

  if (config.protected_vm()) {
    crosvm_cmd.Cmd().AddParameter("--protected-vm");
  }

  if (config.gdb_port() > 0) {
    CHECK(config.cpus() == 1) << "CPUs must be 1 for crosvm gdb mode";
    crosvm_cmd.Cmd().AddParameter("--gdb=", config.gdb_port());
  }

  auto gpu_mode = config.gpu_mode();
  if (gpu_mode == kGpuModeGuestSwiftshader) {
    crosvm_cmd.Cmd().AddParameter("--gpu=2D");
  } else if (gpu_mode == kGpuModeDrmVirgl || gpu_mode == kGpuModeGfxStream) {
    crosvm_cmd.Cmd().AddParameter(
        gpu_mode == kGpuModeGfxStream ? "--gpu=gfxstream," : "--gpu=",
        "egl=true,surfaceless=true,glx=false,gles=true");
  }

  for (const auto& display_config : config.display_configs()) {
    crosvm_cmd.Cmd().AddParameter(
        "--gpu-display=", "width=", display_config.width, ",",
        "height=", display_config.height);
  }

  crosvm_cmd.Cmd().AddParameter("--wayland-sock=",
                                instance.frames_socket_path());

  // crosvm_cmd.Cmd().AddParameter("--null-audio");
  crosvm_cmd.Cmd().AddParameter("--mem=", config.memory_mb());
  crosvm_cmd.Cmd().AddParameter("--cpus=", config.cpus());

  auto disk_num = instance.virtual_disk_paths().size();
  CHECK_GE(VmManager::kMaxDisks, disk_num)
      << "Provided too many disks (" << disk_num << "), maximum "
      << VmManager::kMaxDisks << "supported";
  for (const auto& disk : instance.virtual_disk_paths()) {
    crosvm_cmd.Cmd().AddParameter(
        config.protected_vm() ? "--disk=" : "--rwdisk=", disk);
  }

  if (config.enable_vnc_server() || config.enable_webrtc()) {
    auto touch_type_parameter =
        config.enable_webrtc() ? "--multi-touch=" : "--single-touch=";

    auto display_configs = config.display_configs();
    CHECK_GE(display_configs.size(), 1);

    for (int i = 0; i < display_configs.size(); ++i) {
      auto display_config = display_configs[i];

      crosvm_cmd.Cmd().AddParameter(
          touch_type_parameter, instance.touch_socket_path(i), ":",
          display_config.width, ":", display_config.height);
    }
    crosvm_cmd.Cmd().AddParameter("--keyboard=",
                                  instance.keyboard_socket_path());
  }
  if (config.enable_webrtc()) {
    crosvm_cmd.Cmd().AddParameter("--switches=",
                                  instance.switches_socket_path());
  }

  crosvm_cmd.AddTap(instance.mobile_tap_name());
  crosvm_cmd.AddTap(instance.ethernet_tap_name());

  SharedFD wifi_tap;
  // TODO(b/199103204): remove this as well when PRODUCT_ENFORCE_MAC80211_HWSIM
  // is removed
#ifdef ENFORCE_MAC80211_HWSIM
  if (use_ap_instance) {
    wifi_tap = ap_cmd.AddTap(instance.wifi_tap_name());
  }
#else
  wifi_tap = crosvm_cmd.AddTap(instance.wifi_tap_name());
#endif

  if (FileExists(instance.access_kregistry_path())) {
    crosvm_cmd.Cmd().AddParameter("--rw-pmem-device=",
                                  instance.access_kregistry_path());
  }

  if (FileExists(instance.pstore_path())) {
    crosvm_cmd.Cmd().AddParameter("--pstore=path=", instance.pstore_path(),
                                  ",size=", FileSize(instance.pstore_path()));
  }

  if (config.enable_sandbox()) {
    const bool seccomp_exists = DirectoryExists(config.seccomp_policy_dir());
    const std::string& var_empty_dir = kCrosvmVarEmptyDir;
    const bool var_empty_available = DirectoryExists(var_empty_dir);
    if (!var_empty_available || !seccomp_exists) {
      LOG(FATAL) << var_empty_dir << " is not an existing, empty directory."
                 << "seccomp-policy-dir, " << config.seccomp_policy_dir()
                 << " does not exist " << std::endl;
      return {};
    }
    crosvm_cmd.Cmd().AddParameter("--seccomp-policy-dir=",
                                  config.seccomp_policy_dir());
    ap_cmd.Cmd().AddParameter("--seccomp-policy-dir=",
                              config.seccomp_policy_dir());
  } else {
    crosvm_cmd.Cmd().AddParameter("--disable-sandbox");
    ap_cmd.Cmd().AddParameter("--disable-sandbox");
  }

  if (instance.vsock_guest_cid() >= 2) {
    crosvm_cmd.Cmd().AddParameter("--cid=", instance.vsock_guest_cid());
  }

  // Use a virtio-console instance for the main kernel console. All
  // messages will switch from earlycon to virtio-console after the driver
  // is loaded, and crosvm will append to the kernel log automatically
  crosvm_cmd.AddHvcConsoleReadOnly(instance.kernel_log_pipe_name());

  if (config.console()) {
    // stdin is the only currently supported way to write data to a serial port in
    // crosvm. A file (named pipe) is used here instead of stdout to ensure only
    // the serial port output is received by the console forwarder as crosvm may
    // print other messages to stdout.
    if (config.kgdb() || config.use_bootloader()) {
      crosvm_cmd.AddSerialConsoleReadWrite(instance.console_out_pipe_name(),
                                           instance.console_in_pipe_name());
      // In kgdb mode, we have the interactive console on ttyS0 (both Android's
      // console and kdb), so we can disable the virtio-console port usually
      // allocated to Android's serial console, and redirect it to a sink. This
      // ensures that that the PCI device assignments (and thus sepolicy) don't
      // have to change
      crosvm_cmd.AddHvcSink();
    } else {
      crosvm_cmd.AddSerialSink();
      crosvm_cmd.AddHvcReadWrite(instance.console_out_pipe_name(),
                                 instance.console_in_pipe_name());
    }
  } else {
    // Use an 8250 UART (ISA or platform device) for earlycon, as the
    // virtio-console driver may not be available for early messages
    // In kgdb mode, earlycon is an interactive console, and so early
    // dmesg will go there instead of the kernel.log
    if (config.kgdb() || config.use_bootloader()) {
      crosvm_cmd.AddSerialConsoleReadOnly(instance.kernel_log_pipe_name());
    }

    // as above, create a fake virtio-console 'sink' port when the serial
    // console is disabled, so the PCI device ID assignments don't move
    // around
    crosvm_cmd.AddHvcSink();
  }

  SharedFD log_out_rd, log_out_wr;
  if (!SharedFD::Pipe(&log_out_rd, &log_out_wr)) {
    LOG(ERROR) << "Failed to create log pipe for crosvm's stdout/stderr: "
               << log_out_rd->StrError();
    return {};
  }
  crosvm_cmd.Cmd().RedirectStdIO(Subprocess::StdIOChannel::kStdOut, log_out_wr);
  crosvm_cmd.Cmd().RedirectStdIO(Subprocess::StdIOChannel::kStdErr, log_out_wr);

  Command log_tee_cmd(HostBinaryPath("log_tee"));
  log_tee_cmd.AddParameter("--process_name=crosvm");
  log_tee_cmd.AddParameter("--log_fd_in=", log_out_rd);

  // Serial port for logcat, redirected to a pipe
  crosvm_cmd.AddHvcReadOnly(instance.logcat_pipe_name());

  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("keymaster_fifo_vm.out"),
      instance.PerInstanceInternalPath("keymaster_fifo_vm.in"));
  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
      instance.PerInstanceInternalPath("gatekeeper_fifo_vm.in"));

  if (config.enable_host_bluetooth()) {
    crosvm_cmd.AddHvcReadWrite(
        instance.PerInstanceInternalPath("bt_fifo_vm.out"),
        instance.PerInstanceInternalPath("bt_fifo_vm.in"));
  } else {
    crosvm_cmd.AddHvcSink();
  }
  if (config.enable_gnss_grpc_proxy()) {
    crosvm_cmd.AddHvcReadWrite(
        instance.PerInstanceInternalPath("gnsshvc_fifo_vm.out"),
        instance.PerInstanceInternalPath("gnsshvc_fifo_vm.in"));
  } else {
    crosvm_cmd.AddHvcSink();
  }

  for (auto i = 0; i < VmManager::kMaxDisks - disk_num; i++) {
    crosvm_cmd.AddHvcSink();
  }
  CHECK(crosvm_cmd.HvcNum() + disk_num ==
        VmManager::kMaxDisks + VmManager::kDefaultNumHvcs)
      << "HVC count (" << crosvm_cmd.HvcNum() << ") + disk count (" << disk_num
      << ") is not the expected total of "
      << VmManager::kMaxDisks + VmManager::kDefaultNumHvcs << " devices";

  if (config.enable_audio()) {
    crosvm_cmd.Cmd().AddParameter(
        "--sound=", config.ForDefaultInstance().audio_server_path());
  }

  // TODO(b/162071003): virtiofs crashes without sandboxing, this should be fixed
  if (config.enable_sandbox()) {
    // Set up directory shared with virtiofs
    crosvm_cmd.Cmd().AddParameter(
        "--shared-dir=", instance.PerInstancePath(kSharedDirName),
        ":shared:type=fs");
  }

  // This needs to be the last parameter
  crosvm_cmd.Cmd().AddParameter("--bios=", config.bootloader());

  // Only run the leases workaround if we are not using the new network
  // bridge architecture - in that case, we have a wider DHCP address
  // space and stale leases should be much less of an issue
  if (!FileExists("/var/run/cuttlefish-dnsmasq-cvd-wbr.leases") &&
      wifi_tap->IsOpen()) {
    // TODO(schuffelen): QEMU also needs this and this is not the best place for
    // this code. Find a better place to put it.
    auto lease_file =
        ForCurrentInstance("/var/run/cuttlefish-dnsmasq-cvd-wbr-") + ".leases";
    if (!ReleaseDhcpLeases(lease_file, wifi_tap)) {
      LOG(ERROR) << "Failed to release wifi DHCP leases. Connecting to the wifi "
                 << "network may not work.";
    }
  }
  ap_cmd.Cmd().AddParameter("--root=", config.ap_rootfs_image());
  ap_cmd.Cmd().AddParameter(config.ap_kernel_image());

  std::vector<Command> ret;
  ret.push_back(std::move(crosvm_cmd.Cmd()));
  if (use_ap_instance) {
    ret.push_back(std::move(ap_cmd.Cmd()));
  }
  ret.push_back(std::move(log_tee_cmd));
  return ret;
}

} // namespace vm_manager
} // namespace cuttlefish

