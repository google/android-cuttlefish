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
#include <vulkan/vulkan.h>

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

std::string GetControlSocketPath(const CuttlefishConfig& config) {
  return config.ForDefaultInstance()
      .PerInstanceInternalPath("crosvm_control.sock");
}

SharedFD AddTapFdParameter(Command* crosvm_cmd,
                                const std::string& tap_name) {
  auto tap_fd = OpenTapInterface(tap_name);
  if (tap_fd->IsOpen()) {
    crosvm_cmd->AddParameter("--tap-fd=", tap_fd);
  } else {
    LOG(ERROR) << "Unable to connect to " << tap_name << ": "
               << tap_fd->StrError();
  }
  return tap_fd;
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

bool Stop() {
  auto config = CuttlefishConfig::Get();
  Command command(config->crosvm_binary());
  command.AddParameter("stop");
  command.AddParameter(GetControlSocketPath(*config));

  auto process = command.Start();

  return process.Wait() == 0;
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

std::vector<Command> CrosvmManager::StartCommands(
    const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  Command crosvm_cmd(config.crosvm_binary(), [](Subprocess* proc) {
    auto stopped = Stop();
    if (stopped) {
      return true;
    }
    LOG(WARNING) << "Failed to stop VMM nicely, attempting to KILL";
    return KillSubprocess(proc);
  });

  int hvc_num = 0;
  int serial_num = 0;
  auto add_hvc_sink = [&crosvm_cmd, &hvc_num]() {
    crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=", ++hvc_num,
                            ",type=sink");
  };
  auto add_serial_sink = [&crosvm_cmd, &serial_num]() {
    crosvm_cmd.AddParameter("--serial=hardware=serial,num=", ++serial_num,
                            ",type=sink");
  };
  auto add_hvc_console = [&crosvm_cmd, &hvc_num](const std::string& output) {
    crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=", ++hvc_num,
                            ",type=file,path=", output, ",console=true");
  };
  auto add_serial_console_ro = [&crosvm_cmd,
                                &serial_num](const std::string& output) {
    crosvm_cmd.AddParameter("--serial=hardware=serial,num=", ++serial_num,
                            ",type=file,path=", output, ",earlycon=true");
  };
  auto add_serial_console = [&crosvm_cmd, &serial_num](
                                const std::string& output,
                                const std::string& input) {
    crosvm_cmd.AddParameter("--serial=hardware=serial,num=", ++serial_num,
                            ",type=file,path=", output, ",input=", input,
                            ",earlycon=true");
  };
  auto add_hvc_ro = [&crosvm_cmd, &hvc_num](const std::string& output) {
    crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=", ++hvc_num,
                            ",type=file,path=", output);
  };
  auto add_hvc = [&crosvm_cmd, &hvc_num](const std::string& output,
                                         const std::string& input) {
    crosvm_cmd.AddParameter("--serial=hardware=virtio-console,num=", ++hvc_num,
                            ",type=file,path=", output, ",input=", input);
  };
  // Deprecated; do not add any more users
  auto add_serial = [&crosvm_cmd, &serial_num](const std::string& output,
                                               const std::string& input) {
    crosvm_cmd.AddParameter("--serial=hardware=serial,num=", ++serial_num,
                            ",type=file,path=", output, ",input=", input);
  };

  crosvm_cmd.AddParameter("run");

  if (!config.smt()) {
    crosvm_cmd.AddParameter("--no-smt");
  }

  if (config.vhost_net()) {
    crosvm_cmd.AddParameter("--vhost-net");
  }

  if (config.protected_vm()) {
    crosvm_cmd.AddParameter("--protected-vm");
  }

  if (config.gdb_port() > 0) {
    CHECK(config.cpus() == 1) << "CPUs must be 1 for crosvm gdb mode";
    crosvm_cmd.AddParameter("--gdb=", config.gdb_port());
  }

  auto display_configs = config.display_configs();
  CHECK_GE(display_configs.size(), 1);
  auto display_config = display_configs[0];

  auto gpu_mode = config.gpu_mode();

  if (gpu_mode == kGpuModeGuestSwiftshader) {
    crosvm_cmd.AddParameter("--gpu=2D,",
                            "width=", display_config.width, ",",
                            "height=", display_config.height);
  } else if (gpu_mode == kGpuModeDrmVirgl || gpu_mode == kGpuModeGfxStream) {
    crosvm_cmd.AddParameter(gpu_mode == kGpuModeGfxStream ?
                                "--gpu=gfxstream," : "--gpu=",
                            "width=", display_config.width, ",",
                            "height=", display_config.height, ",",
                            "egl=true,surfaceless=true,glx=false,gles=true");
  }
  crosvm_cmd.AddParameter("--wayland-sock=", instance.frames_socket_path());

  // crosvm_cmd.AddParameter("--null-audio");
  crosvm_cmd.AddParameter("--mem=", config.memory_mb());
  crosvm_cmd.AddParameter("--cpus=", config.cpus());

  auto disk_num = instance.virtual_disk_paths().size();
  CHECK_GE(VmManager::kMaxDisks, disk_num)
      << "Provided too many disks (" << disk_num << "), maximum "
      << VmManager::kMaxDisks << "supported";
  for (const auto& disk : instance.virtual_disk_paths()) {
    crosvm_cmd.AddParameter(config.protected_vm() ? "--disk=" :
                                                    "--rwdisk=", disk);
  }
  crosvm_cmd.AddParameter("--socket=", GetControlSocketPath(config));

  if (config.enable_vnc_server() || config.enable_webrtc()) {
    auto touch_type_parameter =
        config.enable_webrtc() ? "--multi-touch=" : "--single-touch=";
    crosvm_cmd.AddParameter(touch_type_parameter, instance.touch_socket_path(),
                            ":", display_config.width, ":",
                            display_config.height);
    crosvm_cmd.AddParameter("--keyboard=", instance.keyboard_socket_path());
  }
  if (config.enable_webrtc()) {
    crosvm_cmd.AddParameter("--switches=", instance.switches_socket_path());
  }

  auto wifi_tap = AddTapFdParameter(&crosvm_cmd, instance.wifi_tap_name());
  AddTapFdParameter(&crosvm_cmd, instance.mobile_tap_name());

  if (FileExists(instance.access_kregistry_path())) {
    crosvm_cmd.AddParameter("--rw-pmem-device=",
                            instance.access_kregistry_path());
  }

  if (FileExists(instance.pstore_path())) {
    crosvm_cmd.AddParameter("--pstore=path=", instance.pstore_path(),
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
    crosvm_cmd.AddParameter("--seccomp-policy-dir=", config.seccomp_policy_dir());
  } else {
    crosvm_cmd.AddParameter("--disable-sandbox");
  }

  if (instance.vsock_guest_cid() >= 2) {
    crosvm_cmd.AddParameter("--cid=", instance.vsock_guest_cid());
  }

  // Use a virtio-console instance for the main kernel console. All
  // messages will switch from earlycon to virtio-console after the driver
  // is loaded, and crosvm will append to the kernel log automatically
  add_hvc_console(instance.kernel_log_pipe_name());

  if (config.console()) {
    // stdin is the only currently supported way to write data to a serial port in
    // crosvm. A file (named pipe) is used here instead of stdout to ensure only
    // the serial port output is received by the console forwarder as crosvm may
    // print other messages to stdout.
    if (config.kgdb() || config.use_bootloader()) {
      add_serial_console(instance.console_out_pipe_name(),
                         instance.console_in_pipe_name());
      // In kgdb mode, we have the interactive console on ttyS0 (both Android's
      // console and kdb), so we can disable the virtio-console port usually
      // allocated to Android's serial console, and redirect it to a sink. This
      // ensures that that the PCI device assignments (and thus sepolicy) don't
      // have to change
      add_hvc_sink();
    } else {
      add_serial_sink();
      add_hvc(instance.console_out_pipe_name(),
              instance.console_in_pipe_name());
    }
  } else {
    // Use an 8250 UART (ISA or platform device) for earlycon, as the
    // virtio-console driver may not be available for early messages
    // In kgdb mode, earlycon is an interactive console, and so early
    // dmesg will go there instead of the kernel.log
    if (config.kgdb() || config.use_bootloader()) {
      add_serial_console_ro(instance.kernel_log_pipe_name());
    }

    // as above, create a fake virtio-console 'sink' port when the serial
    // console is disabled, so the PCI device ID assignments don't move
    // around
    add_hvc_sink();
  }

  if (config.enable_gnss_grpc_proxy()) {
    add_serial(instance.gnss_out_pipe_name(), instance.gnss_in_pipe_name());
  }

  SharedFD log_out_rd, log_out_wr;
  if (!SharedFD::Pipe(&log_out_rd, &log_out_wr)) {
    LOG(ERROR) << "Failed to create log pipe for crosvm's stdout/stderr: "
               << log_out_rd->StrError();
    return {};
  }
  crosvm_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, log_out_wr);
  crosvm_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, log_out_wr);

  Command log_tee_cmd(HostBinaryPath("log_tee"));
  log_tee_cmd.AddParameter("--process_name=crosvm");
  log_tee_cmd.AddParameter("--log_fd_in=", log_out_rd);

  // Serial port for logcat, redirected to a pipe
  add_hvc_ro(instance.logcat_pipe_name());

  add_hvc(instance.PerInstanceInternalPath("keymaster_fifo_vm.out"),
          instance.PerInstanceInternalPath("keymaster_fifo_vm.in"));
  add_hvc(instance.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
          instance.PerInstanceInternalPath("gatekeeper_fifo_vm.in"));

  if (config.enable_host_bluetooth()) {
    add_hvc(instance.PerInstanceInternalPath("bt_fifo_vm.out"),
            instance.PerInstanceInternalPath("bt_fifo_vm.in"));
  } else {
    add_hvc_sink();
  }
  for (auto i = 0; i < VmManager::kMaxDisks - disk_num; i++) {
    add_hvc_sink();
  }
  CHECK(hvc_num + disk_num == VmManager::kMaxDisks + VmManager::kDefaultNumHvcs)
      << "HVC count (" << hvc_num << ") + disk count (" << disk_num << ") "
      << "is not the expected total of "
      << VmManager::kMaxDisks + VmManager::kDefaultNumHvcs << " devices";

  if (config.enable_audio()) {
    crosvm_cmd.AddParameter("--ac97=backend=vios,server=" +
                            config.ForDefaultInstance().audio_server_path());
  }

  // TODO(b/172286896): This is temporarily optional, but should be made
  // unconditional and moved up to the other network devices area
  if (config.ethernet()) {
    AddTapFdParameter(&crosvm_cmd, instance.ethernet_tap_name());
  }

  // TODO(b/162071003): virtiofs crashes without sandboxing, this should be fixed
  if (config.enable_sandbox()) {
    // Set up directory shared with virtiofs
    crosvm_cmd.AddParameter("--shared-dir=", instance.PerInstancePath(kSharedDirName),
                            ":shared:type=fs");
  }

  // This needs to be the last parameter
  crosvm_cmd.AddParameter("--bios=", config.bootloader());

  // Only run the leases workaround if we are not using the new network
  // bridge architecture - in that case, we have a wider DHCP address
  // space and stale leases should be much less of an issue
  if (!FileExists("/var/run/cuttlefish-dnsmasq-cvd-wbr.leases")) {
    // TODO(schuffelen): QEMU also needs this and this is not the best place for
    // this code. Find a better place to put it.
    auto lease_file =
        ForCurrentInstance("/var/run/cuttlefish-dnsmasq-cvd-wbr-") + ".leases";
    if (!ReleaseDhcpLeases(lease_file, wifi_tap)) {
      LOG(ERROR) << "Failed to release wifi DHCP leases. Connecting to the wifi "
                 << "network may not work.";
    }
  }

  std::vector<Command> ret;
  ret.push_back(std::move(crosvm_cmd));
  ret.push_back(std::move(log_tee_cmd));
  return ret;
}

} // namespace vm_manager
} // namespace cuttlefish

