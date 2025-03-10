/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "host/libs/vm_manager/qemu_manager.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <android-base/strings.h>
#include <android-base/logging.h>
#include <vulkan/vulkan.h>

#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace vm_manager {
namespace {

std::string GetMonitorPath(const CuttlefishConfig& config) {
  return config.ForDefaultInstance()
      .PerInstanceInternalPath("qemu_monitor.sock");
}

void LogAndSetEnv(const char* key, const std::string& value) {
  setenv(key, value.c_str(), 1);
  LOG(INFO) << key << "=" << value;
}

bool Stop() {
  auto config = CuttlefishConfig::Get();
  auto monitor_path = GetMonitorPath(*config);
  auto monitor_sock = SharedFD::SocketLocalClient(
      monitor_path.c_str(), false, SOCK_STREAM);

  if (!monitor_sock->IsOpen()) {
    LOG(ERROR) << "The connection to qemu is closed, is it still running?";
    return false;
  }
  char msg[] = "{\"execute\":\"qmp_capabilities\"}{\"execute\":\"quit\"}";
  ssize_t len = sizeof(msg) - 1;
  while (len > 0) {
    int tmp = monitor_sock->Write(msg, len);
    if (tmp < 0) {
      LOG(ERROR) << "Error writing to socket: " << monitor_sock->StrError();
      return false;
    }
    len -= tmp;
  }
  // Log the reply
  char buff[1000];
  while ((len = monitor_sock->Read(buff, sizeof(buff) - 1)) > 0) {
    buff[len] = '\0';
    LOG(INFO) << "From qemu monitor: " << buff;
  }

  return true;
}

Result<std::pair<int, int>> GetQemuVersion(const std::string& qemu_binary) {
  Command qemu_version_cmd(qemu_binary);
  qemu_version_cmd.AddParameter("-version");

  std::string qemu_version_input, qemu_version_output, qemu_version_error;
  cuttlefish::SubprocessOptions options;
  options.Verbose(false);
  int qemu_version_ret =
      cuttlefish::RunWithManagedStdio(std::move(qemu_version_cmd),
                                      &qemu_version_input,
                                      &qemu_version_output,
                                      &qemu_version_error, options);
  CF_EXPECT(qemu_version_ret == 0,
            qemu_binary << " -version returned unexpected response "
                        << qemu_version_output << ". Stderr was "
                        << qemu_version_error);

  // Snip around the extra text we don't care about
  qemu_version_output.erase(0, std::string("QEMU emulator version ").length());
  auto space_pos = qemu_version_output.find(" ", 0);
  if (space_pos != std::string::npos) {
    qemu_version_output.resize(space_pos);
  }

  auto qemu_version_bits = android::base::Split(qemu_version_output, ".");
  return {{std::stoi(qemu_version_bits[0]), std::stoi(qemu_version_bits[1])}};
}

}  // namespace

QemuManager::QemuManager(Arch arch) : arch_(arch) {}

bool QemuManager::IsSupported() {
  return HostSupportsQemuCli();
}

std::vector<std::string> QemuManager::ConfigureGraphics(
    const CuttlefishConfig& config) {
  if (config.gpu_mode() == kGpuModeGuestSwiftshader) {
    // Override the default HAL search paths in all cases. We do this because
    // the HAL search path allows for fallbacks, and fallbacks in conjunction
    // with properities lead to non-deterministic behavior while loading the
    // HALs.
    return {
        "androidboot.cpuvulkan.version=" + std::to_string(VK_API_VERSION_1_2),
        "androidboot.hardware.gralloc=minigbm",
        "androidboot.hardware.hwcomposer=" + config.hwcomposer(),
        "androidboot.hardware.egl=angle",
        "androidboot.hardware.vulkan=pastel",
        "androidboot.opengles.version=196609"};  // OpenGL ES 3.1
  }

  if (config.gpu_mode() == kGpuModeDrmVirgl) {
    return {
      "androidboot.cpuvulkan.version=0",
      "androidboot.hardware.gralloc=minigbm",
      "androidboot.hardware.hwcomposer=ranchu",
      "androidboot.hardware.hwcomposer.mode=client",
      "androidboot.hardware.egl=mesa",
      // No "hardware" Vulkan support, yet
      "androidboot.opengles.version=196608"};  // OpenGL ES 3.0
  }

  return {};
}

std::string QemuManager::ConfigureBootDevices(int num_disks) {
  switch (arch_) {
    case Arch::X86:
    case Arch::X86_64: {
      // QEMU has additional PCI devices for an ISA bridge and PIIX4
      // virtio_gpu precedes the first console or disk
      return ConfigureMultipleBootDevices("pci0000:00/0000:00:", 3, num_disks);
    }
    case Arch::Arm:
      return "androidboot.boot_devices=3f000000.pcie";
    case Arch::Arm64:
      return "androidboot.boot_devices=4010000000.pcie";
  }
}

Result<std::vector<Command>> QemuManager::StartCommands(
    const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();

  auto stop = [](Subprocess* proc) {
    auto stopped = Stop();
    if (stopped) {
      return StopperResult::kStopSuccess;
    }
    LOG(WARNING) << "Failed to stop VMM nicely, "
                  << "attempting to KILL";
    return KillSubprocess(proc) == StopperResult::kStopSuccess
               ? StopperResult::kStopCrash
               : StopperResult::kStopFailure;
  };
  std::string qemu_binary = config.qemu_binary_dir();
  switch (arch_) {
    case Arch::Arm:
      qemu_binary += "/qemu-system-arm";
      break;
    case Arch::Arm64:
      qemu_binary += "/qemu-system-aarch64";
      break;
    case Arch::X86:
      qemu_binary += "/qemu-system-i386";
      break;
    case Arch::X86_64:
      qemu_binary += "/qemu-system-x86_64";
      break;
  }

  auto qemu_version = CF_EXPECT(GetQemuVersion(qemu_binary));
  Command qemu_cmd(qemu_binary, stop);

  int hvc_num = 0;
  int serial_num = 0;
  auto add_hvc_sink = [&qemu_cmd, &hvc_num]() {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("null,id=hvc", hvc_num);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter(
        "virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial",
        hvc_num);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtconsole,bus=virtio-serial", hvc_num,
                          ".0,chardev=hvc", hvc_num);
    hvc_num++;
  };
  auto add_serial_sink = [&qemu_cmd, &serial_num]() {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("null,id=serial", serial_num);
    qemu_cmd.AddParameter("-serial");
    qemu_cmd.AddParameter("chardev:serial", serial_num);
    serial_num++;
  };
  auto add_serial_console_ro = [&qemu_cmd,
                                &serial_num](const std::string& output) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("file,id=serial", serial_num, ",path=", output,
                          ",append=on");
    qemu_cmd.AddParameter("-serial");
    qemu_cmd.AddParameter("chardev:serial", serial_num);
    serial_num++;
  };
  auto add_serial_console = [&qemu_cmd,
                             &serial_num](const std::string& prefix) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("pipe,id=serial", serial_num, ",path=", prefix);
    qemu_cmd.AddParameter("-serial");
    qemu_cmd.AddParameter("chardev:serial", serial_num);
    serial_num++;
  };
  auto add_hvc_ro = [&qemu_cmd, &hvc_num](const std::string& output) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("file,id=hvc", hvc_num, ",path=", output,
                          ",append=on");
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter(
        "virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial",
        hvc_num);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtconsole,bus=virtio-serial", hvc_num,
                          ".0,chardev=hvc", hvc_num);
    hvc_num++;
  };
  auto add_hvc = [&qemu_cmd, &hvc_num](const std::string& prefix) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("pipe,id=hvc", hvc_num, ",path=", prefix);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter(
        "virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial",
        hvc_num);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtconsole,bus=virtio-serial", hvc_num,
                          ".0,chardev=hvc", hvc_num);
    hvc_num++;
  };

  bool is_arm = arch_ == Arch::Arm || arch_ == Arch::Arm64;
  bool is_arm64 = arch_ == Arch::Arm64;

  auto access_kregistry_size_bytes = 0;
  if (FileExists(instance.access_kregistry_path())) {
    access_kregistry_size_bytes = FileSize(instance.access_kregistry_path());
    CF_EXPECT((access_kregistry_size_bytes & (1024 * 1024 - 1)) == 0,
              instance.access_kregistry_path()
                  << " file size (" << access_kregistry_size_bytes
                  << ") not a multiple of 1MB");
  }

  auto hwcomposer_pmem_size_bytes = 0;
  if (FileExists(instance.hwcomposer_pmem_path())) {
    hwcomposer_pmem_size_bytes = FileSize(instance.hwcomposer_pmem_path());
    CF_EXPECT((hwcomposer_pmem_size_bytes & (1024 * 1024 - 1)) == 0,
              instance.hwcomposer_pmem_path()
                  << " file size (" << hwcomposer_pmem_size_bytes
                  << ") not a multiple of 1MB");
  }

  auto pstore_size_bytes = 0;
  if (FileExists(instance.pstore_path())) {
    pstore_size_bytes = FileSize(instance.pstore_path());
    CF_EXPECT((pstore_size_bytes & (1024 * 1024 - 1)) == 0,
              instance.pstore_path() << " file size (" << pstore_size_bytes
                                     << ") not a multiple of 1MB");
  }

  qemu_cmd.AddParameter("-name");
  qemu_cmd.AddParameter("guest=", instance.instance_name(), ",debug-threads=on");

  qemu_cmd.AddParameter("-machine");
  std::string machine = is_arm ? "virt" : "pc-i440fx-2.8,nvdimm=on";
  if (IsHostCompatible(arch_)) {
    machine += ",accel=kvm";
    if (is_arm) {
      machine += ",gic-version=3";
    }
  } else if (is_arm) {
    // QEMU doesn't support GICv3 with TCG yet
    machine += ",gic-version=2";
    if (is_arm64) {
      // Only enable MTE in TCG mode. We haven't started to run on ARMv8/ARMv9
      // devices with KVM and MTE, so MTE will always require TCG
      machine += ",mte=on";
    }
    CF_EXPECT(instance.cpus() <= 8, "CPUs must be no more than 8 with GICv2");
  }
  qemu_cmd.AddParameter(machine, ",usb=off,dump-guest-core=off");

  qemu_cmd.AddParameter("-m");
  auto maxmem = instance.memory_mb() +
                (access_kregistry_size_bytes / 1024 / 1024) +
                (hwcomposer_pmem_size_bytes / 1024 / 1024) +
                (is_arm ? 0 : pstore_size_bytes / 1024 / 1024);
  auto slots = is_arm ? "" : ",slots=2";
  qemu_cmd.AddParameter("size=", instance.memory_mb(), "M",
                        ",maxmem=", maxmem, "M", slots);

  qemu_cmd.AddParameter("-overcommit");
  qemu_cmd.AddParameter("mem-lock=off");

  // Assume SMT is always 2 threads per core, which is how most hardware
  // today is configured, and the way crosvm does it
  qemu_cmd.AddParameter("-smp");
  if (config.smt()) {
    CF_EXPECT(instance.cpus() % 2 == 0,
              "CPUs must be a multiple of 2 in SMT mode");
    qemu_cmd.AddParameter(instance.cpus(), ",cores=",
                          instance.cpus() / 2, ",threads=2");
  } else {
    qemu_cmd.AddParameter(instance.cpus(), ",cores=",
                          instance.cpus(), ",threads=1");
  }

  qemu_cmd.AddParameter("-uuid");
  qemu_cmd.AddParameter(instance.uuid());

  qemu_cmd.AddParameter("-no-user-config");
  qemu_cmd.AddParameter("-nodefaults");
  qemu_cmd.AddParameter("-no-shutdown");

  qemu_cmd.AddParameter("-rtc");
  qemu_cmd.AddParameter("base=utc");

  qemu_cmd.AddParameter("-boot");
  qemu_cmd.AddParameter("strict=on");

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("socket,id=charmonitor,path=", GetMonitorPath(config),
                        ",server=on,wait=off");

  qemu_cmd.AddParameter("-mon");
  qemu_cmd.AddParameter("chardev=charmonitor,id=monitor,mode=control");

  if (config.gpu_mode() == kGpuModeDrmVirgl) {
    qemu_cmd.AddParameter("-display");
    qemu_cmd.AddParameter("egl-headless");

    qemu_cmd.AddParameter("-vnc");
    qemu_cmd.AddParameter(":", instance.qemu_vnc_server_port());
  } else {
    qemu_cmd.AddParameter("-display");
    qemu_cmd.AddParameter("none");
  }

  auto display_configs = instance.display_configs();
  CF_EXPECT(display_configs.size() >= 1);
  auto display_config = display_configs[0];

  qemu_cmd.AddParameter("-device");

  bool use_gpu_gl = qemu_version.first >= 6 &&
                    config.gpu_mode() != kGpuModeGuestSwiftshader;
  qemu_cmd.AddParameter(use_gpu_gl ?
                            "virtio-gpu-gl-pci" : "virtio-gpu-pci", ",id=gpu0",
                        ",xres=", display_config.width,
                        ",yres=", display_config.height);

  if (!instance.console()) {
    // In kgdb mode, earlycon is an interactive console, and so early
    // dmesg will go there instead of the kernel.log. On QEMU, we do this
    // bit of logic up before the hvc console is set up, so the command line
    // flags appear in the right order and "append=on" does the right thing
    if (config.enable_kernel_log() &&
        (instance.kgdb() || instance.use_bootloader())) {
      add_serial_console_ro(instance.kernel_log_pipe_name());
    }
  }

  // If kernel log is enabled, the virtio-console port will be specified as
  // a true console for Linux, and kernel messages will be printed there.
  // Otherwise, the port will still be set up for bootloader and userspace
  // messages, but the kernel will not print anything here. This keeps our
  // kernel log event features working. If an alternative "earlycon" boot
  // console is configured above on a legacy serial port, it will control
  // the main log until the virtio-console takes over.
  // (Note that QEMU does not automatically generate console= parameters for
  //  the bootloader/kernel cmdline, so the control of whether this pipe is
  //  actually managed by the kernel as a console is handled elsewhere.)
  add_hvc_ro(instance.kernel_log_pipe_name());

  if (instance.console()) {
    if (instance.kgdb() || instance.use_bootloader()) {
      add_serial_console(instance.console_pipe_prefix());

      // In kgdb mode, we have the interactive console on ttyS0 (both Android's
      // console and kdb), so we can disable the virtio-console port usually
      // allocated to Android's serial console, and redirect it to a sink. This
      // ensures that that the PCI device assignments (and thus sepolicy) don't
      // have to change
      add_hvc_sink();
    } else {
      add_serial_sink();
      add_hvc(instance.console_pipe_prefix());
    }
  } else {
    if (instance.kgdb() || instance.use_bootloader()) {
      // The add_serial_console_ro() call above was applied by the time we reach
      // this code, so we don't need another add_serial_*() call
    }

    // as above, create a fake virtio-console 'sink' port when the serial
    // console is disabled, so the PCI device ID assignments don't move
    // around
    add_hvc_sink();
  }

  // Serial port for logcat, redirected to a pipe
  add_hvc_ro(instance.logcat_pipe_name());

  add_hvc(instance.PerInstanceInternalPath("keymaster_fifo_vm"));
  add_hvc(instance.PerInstanceInternalPath("gatekeeper_fifo_vm"));
  if (config.enable_host_bluetooth()) {
    add_hvc(instance.PerInstanceInternalPath("bt_fifo_vm"));
  } else {
    add_hvc_sink();
  }

  if (config.enable_gnss_grpc_proxy()) {
    add_hvc(instance.PerInstanceInternalPath("gnsshvc_fifo_vm"));
    add_hvc(instance.PerInstanceInternalPath("locationhvc_fifo_vm"));
  } else {
    for (auto i = 0; i < 2; i++) {
      add_hvc_sink();
    }
  }

  /* Added one for confirmation UI.
   *
   * b/237452165
   *
   * Confirmation UI is not supported with QEMU for now. In order
   * to not conflict with confirmation UI-related configurations used
   * w/ Crosvm, we should add one generic avc.
   *
   * confui_fifo_vm.{in/out} are created along with the streamer process,
   * which is not created w/ QEMU.
   */
  add_hvc_sink();

  auto disk_num = instance.virtual_disk_paths().size();

  for (auto i = 0; i < VmManager::kMaxDisks - disk_num; i++) {
    add_hvc_sink();
  }

  CF_EXPECT(
      hvc_num + disk_num == VmManager::kMaxDisks + VmManager::kDefaultNumHvcs,
      "HVC count (" << hvc_num << ") + disk count (" << disk_num << ") "
                    << "is not the expected total of "
                    << VmManager::kMaxDisks + VmManager::kDefaultNumHvcs
                    << " devices");

  CF_EXPECT(VmManager::kMaxDisks >= disk_num,
            "Provided too many disks (" << disk_num << "), maximum "
                                        << VmManager::kMaxDisks << "supported");
  auto readonly = config.protected_vm() ? ",readonly" : "";
  for (size_t i = 0; i < disk_num; i++) {
    auto bootindex = i == 0 ? ",bootindex=1" : "";
    auto format = i == 0 ? "" : ",format=raw";
    auto disk = instance.virtual_disk_paths()[i];
    qemu_cmd.AddParameter("-drive");
    qemu_cmd.AddParameter("file=", disk, ",if=none,id=drive-virtio-disk", i,
                          ",aio=threads", format, readonly);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtio-blk-pci-non-transitional,scsi=off,drive=drive-virtio-disk", i,
                          ",id=virtio-disk", i, bootindex);
  }

  if (!is_arm && FileExists(instance.pstore_path())) {
    // QEMU will assign the NVDIMM (ramoops pstore region) 100000000-1001fffff
    // As we will pass this to ramoops, define this region first so it is always
    // located at this address. This is currently x86 only.
    qemu_cmd.AddParameter("-object");
    qemu_cmd.AddParameter("memory-backend-file,id=objpmem0,share=on,mem-path=",
                          instance.pstore_path(), ",size=", pstore_size_bytes);

    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("nvdimm,memdev=objpmem0,id=ramoops");
  }

  // QEMU does not implement virtio-pmem-pci for ARM64 yet; restore this
  // when the device has been added
  if (!is_arm) {
    if (access_kregistry_size_bytes > 0) {
      qemu_cmd.AddParameter("-object");
      qemu_cmd.AddParameter(
          "memory-backend-file,id=objpmem1,share=on,mem-path=",
          instance.access_kregistry_path(),
          ",size=", access_kregistry_size_bytes);

      qemu_cmd.AddParameter("-device");
      qemu_cmd.AddParameter(
          "virtio-pmem-pci,disable-legacy=on,memdev=objpmem1,id=pmem0");
    }
    if (hwcomposer_pmem_size_bytes > 0) {
      qemu_cmd.AddParameter("-object");
      qemu_cmd.AddParameter(
          "memory-backend-file,id=objpmem2,share=on,mem-path=",
          instance.hwcomposer_pmem_path(),
          ",size=", hwcomposer_pmem_size_bytes);

      qemu_cmd.AddParameter("-device");
      qemu_cmd.AddParameter(
          "virtio-pmem-pci,disable-legacy=on,memdev=objpmem2,id=pmem1");
    }
  }

  qemu_cmd.AddParameter("-object");
  qemu_cmd.AddParameter("rng-random,id=objrng0,filename=/dev/urandom");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-rng-pci-non-transitional,rng=objrng0,id=rng0,",
                        "max-bytes=1024,period=2000");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-mouse-pci,disable-legacy=on");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-keyboard-pci,disable-legacy=on");

  // device padding for unsupported "switches" input
  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-keyboard-pci,disable-legacy=on");

  auto vhost_net = config.vhost_net() ? ",vhost=on" : "";

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-balloon-pci-non-transitional,id=balloon0");

  qemu_cmd.AddParameter("-netdev");
  qemu_cmd.AddParameter("tap,id=hostnet0,ifname=", instance.mobile_tap_name(),
                        ",script=no,downscript=no", vhost_net);

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-net-pci-non-transitional,netdev=hostnet0,id=net0");

  qemu_cmd.AddParameter("-netdev");
  qemu_cmd.AddParameter("tap,id=hostnet1,ifname=", instance.ethernet_tap_name(),
                        ",script=no,downscript=no", vhost_net);

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-net-pci-non-transitional,netdev=hostnet1,id=net1");
#ifndef ENFORCE_MAC80211_HWSIM
  qemu_cmd.AddParameter("-netdev");
  qemu_cmd.AddParameter("tap,id=hostnet2,ifname=", instance.wifi_tap_name(),
                        ",script=no,downscript=no", vhost_net);
  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-net-pci-non-transitional,netdev=hostnet2,id=net2");
#endif

  qemu_cmd.AddParameter("-cpu");
  qemu_cmd.AddParameter(IsHostCompatible(arch_) ? "host" : "max");

  qemu_cmd.AddParameter("-msg");
  qemu_cmd.AddParameter("timestamp=on");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("vhost-vsock-pci-non-transitional,guest-cid=",
                        instance.vsock_guest_cid());

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("AC97");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("qemu-xhci,id=xhci");

  qemu_cmd.AddParameter("-bios");
  qemu_cmd.AddParameter(instance.bootloader());

  if (instance.gdb_port() > 0) {
    qemu_cmd.AddParameter("-S");
    qemu_cmd.AddParameter("-gdb");
    qemu_cmd.AddParameter("tcp::", instance.gdb_port());
  }

  LogAndSetEnv("QEMU_AUDIO_DRV", "none");

  std::vector<Command> ret;
  ret.push_back(std::move(qemu_cmd));
  return ret;
}

} // namespace vm_manager
}  // namespace cuttlefish
