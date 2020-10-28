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

}  // namespace

/* static */ std::string QemuManager::name() { return "qemu_cli"; }

bool QemuManager::IsSupported() {
  return HostSupportsQemuCli();
}

std::vector<std::string> QemuManager::ConfigureGpuMode(
    const std::string& gpu_mode) {
  if (gpu_mode == kGpuModeGuestSwiftshader) {
    // Override the default HAL search paths in all cases. We do this because
    // the HAL search path allows for fallbacks, and fallbacks in conjunction
    // with properities lead to non-deterministic behavior while loading the
    // HALs.
    return {
      "androidboot.cpuvulkan.version=" + std::to_string(VK_API_VERSION_1_1),
      "androidboot.hardware.gralloc=minigbm",
      "androidboot.hardware.hwcomposer=cutf",
      "androidboot.hardware.egl=swiftshader",
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

  return {};
}

std::vector<std::string> QemuManager::ConfigureBootDevices() {
  // PCI domain 0, bus 0, device 7, function 0
  return { "androidboot.boot_devices=pci0000:00/0000:00:07.0" };
}

std::vector<Command> QemuManager::StartCommands(
    const CuttlefishConfig& config, const std::string& kernel_cmdline) {
  auto instance = config.ForDefaultInstance();

  auto stop = [](Subprocess* proc) {
    auto stopped = Stop();
    if (stopped) {
      return true;
    }
    LOG(WARNING) << "Failed to stop VMM nicely, "
                  << "attempting to KILL";
    return KillSubprocess(proc);
  };

  bool is_arm = android::base::EndsWith(config.qemu_binary(), "system-aarch64");

  auto access_kregistry_size_bytes = FileSize(instance.access_kregistry_path());
  if (access_kregistry_size_bytes & (1024 * 1024 - 1)) {
      LOG(FATAL) << instance.access_kregistry_path() <<  " file size ("
                 << access_kregistry_size_bytes << ") not a multiple of 1MB";
      return {};
  }

  auto pstore_size_bytes = FileSize(instance.pstore_path());
  if (pstore_size_bytes & (1024 * 1024 - 1)) {
      LOG(FATAL) << instance.pstore_path() <<  " file size ("
                 << pstore_size_bytes << ") not a multiple of 1MB";
      return {};
  }

  Command qemu_cmd(config.qemu_binary(), stop);
  qemu_cmd.AddParameter("-name");
  qemu_cmd.AddParameter("guest=", instance.instance_name(), ",debug-threads=on");

  qemu_cmd.AddParameter("-machine");
  auto machine = is_arm ? "virt,gic-version=2" : "pc-i440fx-2.8,accel=kvm,nvdimm=on";
  qemu_cmd.AddParameter(machine, ",usb=off,dump-guest-core=off");

  qemu_cmd.AddParameter("-m");
  auto maxmem = config.memory_mb() +
                access_kregistry_size_bytes / 1024 / 1024 +
                (is_arm ? 0 : pstore_size_bytes / 1024 / 1024);
  auto slots = is_arm ? "" : ",slots=2";
  qemu_cmd.AddParameter("size=", config.memory_mb(), "M",
                        ",maxmem=", maxmem, "M", slots);

  qemu_cmd.AddParameter("-overcommit");
  qemu_cmd.AddParameter("mem-lock=off");

  qemu_cmd.AddParameter("-smp");
  qemu_cmd.AddParameter(config.cpus(), ",sockets=", config.cpus(),
                        ",cores=1,threads=1");

  qemu_cmd.AddParameter("-uuid");
  qemu_cmd.AddParameter(instance.uuid());

  qemu_cmd.AddParameter("-no-user-config");
  qemu_cmd.AddParameter("-nodefaults");
  qemu_cmd.AddParameter("-no-shutdown");

  qemu_cmd.AddParameter("-rtc");
  qemu_cmd.AddParameter("base=utc");

  qemu_cmd.AddParameter("-boot");
  qemu_cmd.AddParameter("strict=on");

  qemu_cmd.AddParameter("-kernel");
  qemu_cmd.AddParameter(config.GetKernelImageToUse());

  qemu_cmd.AddParameter("-append");
  qemu_cmd.AddParameter(kernel_cmdline);

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("socket,id=charmonitor,path=", GetMonitorPath(config),
                        ",server,nowait");

  qemu_cmd.AddParameter("-mon");
  qemu_cmd.AddParameter("chardev=charmonitor,id=monitor,mode=control");

  // In kgdb mode, earlycon is an interactive console, and so early
  // dmesg will go there instead of the kernel.log
  if (!(config.console() && (config.kgdb() || config.use_bootloader()))) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("file,id=earlycon,path=",
                          instance.kernel_log_pipe_name(), ",append=on");

    // On ARM, -serial will imply an AMBA pl011 serial port. On x86, -serial
    // will imply an ISA serial port. We have set up earlycon for each of these
    // port types, so the setting here should match
    qemu_cmd.AddParameter("-serial");
    qemu_cmd.AddParameter("chardev:earlycon");
  }

  // This sets up the HVC (virtio-serial / virtio-console) port for the kernel
  // logging. This will take over the earlycon logging when the module is
  // loaded. Give it the first nr, so it gets /dev/hvc0.
  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("file,id=hvc0,path=",
                        instance.kernel_log_pipe_name(), ",append=on");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial0");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtconsole,bus=virtio-serial0.0,chardev=hvc0");

  // This handles the Android interactive serial console - /dev/hvc1

  if (config.console()) {
    if (config.kgdb() || config.use_bootloader()) {
      qemu_cmd.AddParameter("-chardev");
      qemu_cmd.AddParameter("pipe,id=earlycon,path=", instance.console_pipe_prefix());

      // On ARM, -serial will imply an AMBA pl011 serial port. On x86, -serial
      // will imply an ISA serial port. We have set up earlycon for each of these
      // port types, so the setting here should match
      qemu_cmd.AddParameter("-serial");
      qemu_cmd.AddParameter("chardev:earlycon");

      // In kgdb mode, we have the interactive console on ttyS0 (both Android's
      // console and kdb), so we can disable the virtio-console port usually
      // allocated to Android's serial console, and redirect it to a sink. This
      // ensures that that the PCI device assignments (and thus sepolicy) don't
      // have to change
      qemu_cmd.AddParameter("-chardev");
      qemu_cmd.AddParameter("null,id=hvc1");
    } else {
      qemu_cmd.AddParameter("-chardev");
      qemu_cmd.AddParameter("pipe,id=hvc1,path=", instance.console_pipe_prefix());
    }
  } else {
    // as above, create a fake virtio-console 'sink' port when the serial
    // console is disabled, so the PCI device ID assignments don't move
    // around
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("null,id=hvc1");
  }

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial1");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtconsole,bus=virtio-serial1.0,chardev=hvc1");

  if (config.enable_gnss_grpc_proxy()) {
      qemu_cmd.AddParameter("-chardev");
      qemu_cmd.AddParameter("pipe,id=gnss,path=", instance.gnss_pipe_prefix());

      qemu_cmd.AddParameter("-serial");
      qemu_cmd.AddParameter("chardev:gnss");
  }

  // If configured, this handles logcat forwarding to the host via serial
  // (instead of vsocket) - /dev/hvc2

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("file,id=hvc2,path=",
                        instance.logcat_pipe_name(), ",append=on");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial2");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtconsole,bus=virtio-serial2.0,chardev=hvc2");

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("pipe,id=hvc3,path=",
                        instance.PerInstanceInternalPath("keymaster_fifo_vm"));

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial3");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtconsole,bus=virtio-serial3.0,chardev=hvc3");

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("pipe,id=hvc4,path=",
                        instance.PerInstanceInternalPath("gatekeeper_fifo_vm"));

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial4");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtconsole,bus=virtio-serial4.0,chardev=hvc4");

  for (size_t i = 0; i < instance.virtual_disk_paths().size(); i++) {
    auto bootindex = i == 0 ? ",bootindex=1" : "";
    auto format = i == 0 ? "" : ",format=raw";
    auto disk = instance.virtual_disk_paths()[i];
    qemu_cmd.AddParameter("-drive");
    qemu_cmd.AddParameter("file=", disk, ",if=none,id=drive-virtio-disk", i,
                          ",aio=threads", format);
    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtio-blk-pci-non-transitional,scsi=off,drive=drive-virtio-disk", i,
                          ",id=virtio-disk", i, bootindex);
  }

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-mouse-pci");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-keyboard-pci");

  if (config.gpu_mode() == kGpuModeDrmVirgl) {
    qemu_cmd.AddParameter("-display");
    qemu_cmd.AddParameter("egl-headless");

    qemu_cmd.AddParameter("-vnc");
    qemu_cmd.AddParameter(":", instance.vnc_server_port() - 5900);
  } else {
    qemu_cmd.AddParameter("-display");
    qemu_cmd.AddParameter("none");
  }

  if (!is_arm) {
    // QEMU will assign the NVDIMM (ramoops pstore region) 100000000-1001fffff
    // As we will pass this to ramoops, define this region first so it is always
    // located at this address. This is currently x86 only.
    qemu_cmd.AddParameter("-object");
    qemu_cmd.AddParameter("memory-backend-file,id=objpmem0,share,mem-path=",
                          instance.pstore_path(), ",size=", pstore_size_bytes);

    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("nvdimm,memdev=objpmem0,id=ramoops");
  }

  // QEMU does not implement virtio-pmem-pci for ARM64 yet; restore this
  // when the device has been added
  if (!is_arm) {
    qemu_cmd.AddParameter("-object");
    qemu_cmd.AddParameter("memory-backend-file,id=objpmem1,share,mem-path=",
                          instance.access_kregistry_path(), ",size=",
                          access_kregistry_size_bytes);

    qemu_cmd.AddParameter("-device");
    qemu_cmd.AddParameter("virtio-pmem-pci,disable-legacy=on,memdev=objpmem1,id=pmem0");
  }

  qemu_cmd.AddParameter("-object");
  qemu_cmd.AddParameter("rng-random,id=objrng0,filename=/dev/urandom");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-rng-pci-non-transitional,rng=objrng0,id=rng0,",
                        "max-bytes=1024,period=2000");

  auto vhost_net = config.vhost_net() ? ",vhost=on" : "";

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-balloon-pci-non-transitional,id=balloon0");

  qemu_cmd.AddParameter("-netdev");
  qemu_cmd.AddParameter("tap,id=hostnet0,ifname=", instance.wifi_tap_name(),
                        ",script=no,downscript=no", vhost_net);

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-net-pci-non-transitional,netdev=hostnet0,id=net0");

  qemu_cmd.AddParameter("-netdev");
  qemu_cmd.AddParameter("tap,id=hostnet1,ifname=", instance.mobile_tap_name(),
                        ",script=no,downscript=no", vhost_net);

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-net-pci-non-transitional,netdev=hostnet1,id=net1");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-gpu-pci,id=gpu0");

  qemu_cmd.AddParameter("-cpu");
  qemu_cmd.AddParameter(is_arm ? "cortex-a53" : "host");

  qemu_cmd.AddParameter("-msg");
  qemu_cmd.AddParameter("timestamp=on");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("vhost-vsock-pci-non-transitional,guest-cid=",
                        instance.vsock_guest_cid());

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("AC97");

  if (config.use_bootloader()) {
    qemu_cmd.AddParameter("-bios");
    qemu_cmd.AddParameter(config.bootloader());
  }

  if (config.gdb_flag().size() > 0) {
    qemu_cmd.AddParameter("-gdb");
    qemu_cmd.AddParameter(config.gdb_flag());
  }
  if (!config.use_bootloader()) {
    qemu_cmd.AddParameter("-initrd");
    qemu_cmd.AddParameter(config.final_ramdisk_path());
  }
  LogAndSetEnv("QEMU_AUDIO_DRV", "none");

  std::vector<Command> ret;
  ret.push_back(std::move(qemu_cmd));
  return ret;
}

} // namespace vm_manager
} // namespace cuttlefish

