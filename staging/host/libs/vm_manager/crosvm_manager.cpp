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

#include <android-base/file.h>
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

}  // namespace

bool CrosvmManager::IsSupported() {
#ifdef __ANDROID__
  return true;
#else
  return HostSupportsQemuCli();
#endif
}

std::vector<std::string> CrosvmManager::ConfigureGraphics(
    const CuttlefishConfig::InstanceSpecific& instance) {
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  if (instance.gpu_mode() == kGpuModeGuestSwiftshader) {
    return {
        "androidboot.cpuvulkan.version=" + std::to_string(VK_API_VERSION_1_2),
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=" + instance.hwcomposer(),
        "androidboot.hardware.hwcomposer.display_finder_mode=drm",
        "androidboot.hardware.egl=angle",
        "androidboot.hardware.vulkan=pastel",
        "androidboot.opengles.version=196609",  // OpenGL ES 3.1
    };
  }

  if (instance.gpu_mode() == kGpuModeDrmVirgl) {
    return {
        "androidboot.cpuvulkan.version=0",
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=ranchu",
        "androidboot.hardware.hwcomposer.mode=client",
        "androidboot.hardware.hwcomposer.display_finder_mode=drm",
        "androidboot.hardware.egl=mesa",
        // No "hardware" Vulkan support, yet
        "androidboot.opengles.version=196608",  // OpenGL ES 3.0
    };
  }
  if (instance.gpu_mode() == kGpuModeGfxStream) {
    std::string gles_impl = instance.enable_gpu_angle() ? "angle" : "emulation";
    return {
        "androidboot.cpuvulkan.version=0",
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=" + instance.hwcomposer(),
        "androidboot.hardware.hwcomposer.display_finder_mode=drm",
        "androidboot.hardware.egl=" + gles_impl,
        "androidboot.hardware.vulkan=ranchu",
        "androidboot.hardware.gltransport=virtio-gpu-asg",
        "androidboot.opengles.version=196608",  // OpenGL ES 3.0
    };
  }
  return {};
}

std::string CrosvmManager::ConfigureBootDevices(int num_disks) {
  // TODO There is no way to control this assignment with crosvm (yet)
  if (HostArch() == Arch::X86_64) {
    // crosvm has an additional PCI device for an ISA bridge
    // virtio_gpu and virtio_wl precedes the first console or disk
    return ConfigureMultipleBootDevices("pci0000:00/0000:00:", 3, num_disks);
  } else {
    // On ARM64 crosvm, block devices are on their own bridge, so we don't
    // need to calculate it, and the path is always the same
    return "androidboot.boot_devices=10000.pci";
  }
}

constexpr auto crosvm_socket = "crosvm_control.sock";
constexpr auto process_restarter = "process_restarter";

Result<std::vector<Command>> CrosvmManager::StartCommands(
    const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();

  CrosvmBuilder crosvm_cmd;
  crosvm_cmd.Cmd().SetExecutableAndName(HostBinaryPath(process_restarter));
  crosvm_cmd.Cmd().AddParameter(kCrosvmVmResetExitCode);
  crosvm_cmd.Cmd().AddParameter(config.crosvm_binary());
  // Flag allows exit codes other than 0 or 1, must be before command argument
  crosvm_cmd.Cmd().AddParameter("--extended-status");
  crosvm_cmd.Cmd().AddParameter("run");
  crosvm_cmd.AddControlSocket(GetControlSocketPath(instance, crosvm_socket),
                              config.crosvm_binary());

  if (!config.smt()) {
    crosvm_cmd.Cmd().AddParameter("--no-smt");
  }

  if (config.vhost_net()) {
    crosvm_cmd.Cmd().AddParameter("--vhost-net");
  }

#ifdef ENFORCE_MAC80211_HWSIM
  if (!config.vhost_user_mac80211_hwsim().empty()) {
    crosvm_cmd.Cmd().AddParameter("--vhost-user-mac80211-hwsim=",
                                  config.vhost_user_mac80211_hwsim());
  }
#endif

  if (config.protected_vm()) {
    crosvm_cmd.Cmd().AddParameter("--protected-vm");
  }

  if (instance.gdb_port() > 0) {
    CF_EXPECT(instance.cpus() == 1, "CPUs must be 1 for crosvm gdb mode");
    crosvm_cmd.Cmd().AddParameter("--gdb=", instance.gdb_port());
  }

  const auto gpu_capture_enabled = !instance.gpu_capture_binary().empty();
  const auto gpu_mode = instance.gpu_mode();

  const std::string gpu_angle_string =
      instance.enable_gpu_angle() ? ",angle=true" : "";
  // 256MB so it is small enough for a 32-bit kernel.
  const std::string gpu_pci_bar_size = ",pci-bar-size=268435456";
  const std::string gpu_udmabuf_string =
      instance.enable_gpu_udmabuf() ? ",udmabuf=true" : "";

  const std::string gpu_common_string = gpu_udmabuf_string + gpu_pci_bar_size;
  const std::string gpu_common_3d_string =
      gpu_common_string + ",egl=true,surfaceless=true,glx=false,gles=true";

  if (gpu_mode == kGpuModeGuestSwiftshader) {
    crosvm_cmd.Cmd().AddParameter("--gpu=backend=2D", gpu_common_string);
  } else if (gpu_mode == kGpuModeDrmVirgl) {
    crosvm_cmd.Cmd().AddParameter("--gpu=backend=virglrenderer",
                                  gpu_common_3d_string);
  } else if (gpu_mode == kGpuModeGfxStream) {
    crosvm_cmd.Cmd().AddParameter("--gpu=backend=gfxstream",
                                  gpu_common_3d_string, gpu_angle_string);
  }

  for (const auto& display_config : instance.display_configs()) {
    const auto display_w = std::to_string(display_config.width);
    const auto display_h = std::to_string(display_config.height);
    const auto display_dpi = std::to_string(display_config.dpi);
    const auto display_rr = std::to_string(display_config.refresh_rate_hz);
    const auto display_params = android::base::Join(
        std::vector<std::string>{
            "mode=windowed[" + display_w + "," + display_h + "]",
            "horizontal-dpi=" + display_dpi,
            "vertical-dpi=" + display_dpi,
            "refresh-rate=" + display_rr,
        },
        ",");
    crosvm_cmd.Cmd().AddParameter("--gpu-display=", display_params);
  }

  crosvm_cmd.Cmd().AddParameter("--wayland-sock=",
                                instance.frames_socket_path());

  // crosvm_cmd.Cmd().AddParameter("--null-audio");
  crosvm_cmd.Cmd().AddParameter("--mem=", instance.memory_mb());
  crosvm_cmd.Cmd().AddParameter("--cpus=", instance.cpus());

  auto disk_num = instance.virtual_disk_paths().size();
  CF_EXPECT(VmManager::kMaxDisks >= disk_num,
            "Provided too many disks (" << disk_num << "), maximum "
                                        << VmManager::kMaxDisks << "supported");
  for (const auto& disk : instance.virtual_disk_paths()) {
    if (config.protected_vm()) {
      crosvm_cmd.AddReadOnlyDisk(disk);
    } else {
      crosvm_cmd.AddReadWriteDisk(disk);
    }
  }

  if (config.enable_webrtc()) {
    auto touch_type_parameter =
        config.enable_webrtc() ? "--multi-touch=" : "--single-touch=";

    auto display_configs = instance.display_configs();
    CF_EXPECT(display_configs.size() >= 1);

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

  SharedFD wifi_tap;
  // GPU capture can only support named files and not file descriptors due to
  // having to pass arguments to crosvm via a wrapper script.
  if (!gpu_capture_enabled) {
    crosvm_cmd.AddTap(instance.mobile_tap_name());
    crosvm_cmd.AddTap(instance.ethernet_tap_name());

    // TODO(b/199103204): remove this as well when
    // PRODUCT_ENFORCE_MAC80211_HWSIM is removed
#ifndef ENFORCE_MAC80211_HWSIM
    wifi_tap = crosvm_cmd.AddTap(instance.wifi_tap_name());
#endif
  }

  if (FileExists(instance.access_kregistry_path())) {
    crosvm_cmd.Cmd().AddParameter("--rw-pmem-device=",
                                  instance.access_kregistry_path());
  }

  if (FileExists(instance.hwcomposer_pmem_path())) {
    crosvm_cmd.Cmd().AddParameter("--rw-pmem-device=",
                                  instance.hwcomposer_pmem_path());
  }

  if (FileExists(instance.pstore_path())) {
    crosvm_cmd.Cmd().AddParameter("--pstore=path=", instance.pstore_path(),
                                  ",size=", FileSize(instance.pstore_path()));
  }

  if (instance.enable_sandbox()) {
    const bool seccomp_exists = DirectoryExists(config.seccomp_policy_dir());
    const std::string& var_empty_dir = kCrosvmVarEmptyDir;
    const bool var_empty_available = DirectoryExists(var_empty_dir);
    CF_EXPECT(var_empty_available && seccomp_exists,
              var_empty_dir << " is not an existing, empty directory."
                            << "seccomp-policy-dir, "
                            << config.seccomp_policy_dir()
                            << " does not exist");
    crosvm_cmd.Cmd().AddParameter("--seccomp-policy-dir=",
                                  config.seccomp_policy_dir());
  } else {
    crosvm_cmd.Cmd().AddParameter("--disable-sandbox");
  }

  if (instance.vsock_guest_cid() >= 2) {
    crosvm_cmd.Cmd().AddParameter("--cid=", instance.vsock_guest_cid());
  }

  // If kernel log is enabled, the virtio-console port will be specified as
  // a true console for Linux, and kernel messages will be printed there.
  // Otherwise, the port will still be set up for bootloader and userspace
  // messages, but the kernel will not print anything here. This keeps our
  // kernel log event features working. If an alternative "earlycon" boot
  // console is configured below on a legacy serial port, it will control
  // the main log until the virtio-console takes over.
  crosvm_cmd.AddHvcReadOnly(instance.kernel_log_pipe_name(),
                            config.enable_kernel_log());

  if (instance.console()) {
    // stdin is the only currently supported way to write data to a serial port
    // in crosvm. A file (named pipe) is used here instead of stdout to ensure
    // only the serial port output is received by the console forwarder as
    // crosvm may print other messages to stdout.
    if (instance.kgdb() || instance.use_bootloader()) {
      crosvm_cmd.AddSerialConsoleReadWrite(instance.console_out_pipe_name(),
                                           instance.console_in_pipe_name(),
                                           config.enable_kernel_log());
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
    if (config.enable_kernel_log() &&
        (instance.kgdb() || instance.use_bootloader())) {
      crosvm_cmd.AddSerialConsoleReadOnly(instance.kernel_log_pipe_name());
    }

    // as above, create a fake virtio-console 'sink' port when the serial
    // console is disabled, so the PCI device ID assignments don't move
    // around
    crosvm_cmd.AddHvcSink();
  }

  auto crosvm_logs_path = instance.PerInstanceInternalPath("crosvm.fifo");
  auto crosvm_logs = SharedFD::Fifo(crosvm_logs_path, 0666);
  CF_EXPECT(crosvm_logs->IsOpen(),
            "Failed to create log fifo for crosvm's stdout/stderr: "
                << crosvm_logs->StrError());

  Command crosvm_log_tee_cmd(HostBinaryPath("log_tee"));
  crosvm_log_tee_cmd.AddParameter("--process_name=crosvm");
  crosvm_log_tee_cmd.AddParameter("--log_fd_in=", crosvm_logs);

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
    crosvm_cmd.AddHvcReadWrite(
        instance.PerInstanceInternalPath("locationhvc_fifo_vm.out"),
        instance.PerInstanceInternalPath("locationhvc_fifo_vm.in"));
  } else {
    for (auto i = 0; i < 2; i++) {
      crosvm_cmd.AddHvcSink();
    }
  }

  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("confui_fifo_vm.out"),
      instance.PerInstanceInternalPath("confui_fifo_vm.in"));

  for (auto i = 0; i < VmManager::kMaxDisks - disk_num; i++) {
    crosvm_cmd.AddHvcSink();
  }
  CF_EXPECT(crosvm_cmd.HvcNum() + disk_num ==
                VmManager::kMaxDisks + VmManager::kDefaultNumHvcs,
            "HVC count (" << crosvm_cmd.HvcNum() << ") + disk count ("
                          << disk_num << ") is not the expected total of "
                          << VmManager::kMaxDisks + VmManager::kDefaultNumHvcs
                          << " devices");

  if (config.enable_audio()) {
    crosvm_cmd.Cmd().AddParameter("--sound=", instance.audio_server_path());
  }

  // TODO(b/162071003): virtiofs crashes without sandboxing, this should be
  // fixed
  if (0 && instance.enable_sandbox()) {
    // Set up directory shared with virtiofs
    crosvm_cmd.Cmd().AddParameter(
        "--shared-dir=", instance.PerInstancePath(kSharedDirName),
        ":shared:type=fs");
  }

  // This needs to be the last parameter
  crosvm_cmd.Cmd().AddParameter("--bios=", instance.bootloader());

  // TODO(b/199103204): remove this as well when PRODUCT_ENFORCE_MAC80211_HWSIM
  // is removed
  // Only run the leases workaround if we are not using the new network
  // bridge architecture - in that case, we have a wider DHCP address
  // space and stale leases should be much less of an issue
  if (!FileExists("/var/run/cuttlefish-dnsmasq-cvd-wbr.leases") &&
      wifi_tap->IsOpen()) {
    // TODO(schuffelen): QEMU also needs this and this is not the best place for
    // this code. Find a better place to put it.
    auto lease_file =
        ForCurrentInstance("/var/run/cuttlefish-dnsmasq-cvd-wbr-") + ".leases";

    std::uint8_t dhcp_server_ip[] = {
        192, 168, 96, (std::uint8_t)(ForCurrentInstance(1) * 4 - 3)};
    if (!ReleaseDhcpLeases(lease_file, wifi_tap, dhcp_server_ip)) {
      LOG(ERROR)
          << "Failed to release wifi DHCP leases. Connecting to the wifi "
          << "network may not work.";
    }
  }

  std::vector<Command> ret;

  if (gpu_capture_enabled) {
    const std::string gpu_capture_basename =
        cpp_basename(instance.gpu_capture_binary());

    auto gpu_capture_logs_path =
        instance.PerInstanceInternalPath("gpu_capture.fifo");
    auto gpu_capture_logs = SharedFD::Fifo(gpu_capture_logs_path, 0666);
    CF_EXPECT(gpu_capture_logs->IsOpen(),
              "Failed to create log fifo for gpu capture's stdout/stderr: "
                  << gpu_capture_logs->StrError());

    Command gpu_capture_log_tee_cmd(HostBinaryPath("log_tee"));
    gpu_capture_log_tee_cmd.AddParameter("--process_name=",
                                         gpu_capture_basename);
    gpu_capture_log_tee_cmd.AddParameter("--log_fd_in=", gpu_capture_logs);

    Command gpu_capture_command(instance.gpu_capture_binary());
    if (gpu_capture_basename == "ngfx") {
      // Crosvm depends on command line arguments being passed as multiple
      // arguments but ngfx only allows a single `--args`. To work around this,
      // create a wrapper script that launches crosvm with all of the arguments
      // and pass this wrapper script to ngfx.
      const std::string crosvm_wrapper_path =
          instance.PerInstanceInternalPath("crosvm_wrapper.sh");
      const std::string crosvm_wrapper_content =
          crosvm_cmd.Cmd().AsBashScript(crosvm_logs_path);

      CF_EXPECT(android::base::WriteStringToFile(crosvm_wrapper_content,
                                                 crosvm_wrapper_path));
      CF_EXPECT(MakeFileExecutable(crosvm_wrapper_path));

      gpu_capture_command.AddParameter("--exe=", crosvm_wrapper_path);
      gpu_capture_command.AddParameter("--launch-detached");
      gpu_capture_command.AddParameter("--verbose");
      gpu_capture_command.AddParameter("--activity=Frame Debugger");
    } else {
      // TODO(natsu): renderdoc
      return CF_ERR(
          "Unhandled GPU capture binary: " << instance.gpu_capture_binary());
    }

    gpu_capture_command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                                      gpu_capture_logs);
    gpu_capture_command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr,
                                      gpu_capture_logs);

    ret.push_back(std::move(gpu_capture_log_tee_cmd));
    ret.push_back(std::move(gpu_capture_command));
  } else {
    crosvm_cmd.Cmd().RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                                   crosvm_logs);
    crosvm_cmd.Cmd().RedirectStdIO(Subprocess::StdIOChannel::kStdErr,
                                   crosvm_logs);

    ret.push_back(std::move(crosvm_cmd.Cmd()));
  }

  ret.push_back(std::move(crosvm_log_tee_cmd));
  return ret;
}

}  // namespace vm_manager
}  // namespace cuttlefish
