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

#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace vm_manager {
namespace {

std::string GetMonitorPath(const cuttlefish::CuttlefishConfig* config) {
  return config->ForDefaultInstance()
      .PerInstanceInternalPath("qemu_monitor.sock");
}

void LogAndSetEnv(const char* key, const std::string& value) {
  setenv(key, value.c_str(), 1);
  LOG(INFO) << key << "=" << value;
}

bool Stop() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto monitor_path = GetMonitorPath(config);
  auto monitor_sock = cuttlefish::SharedFD::SocketLocalClient(
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

const std::string QemuManager::name() { return "qemu_cli"; }

std::vector<std::string> QemuManager::ConfigureGpu(const std::string& gpu_mode) {
  if (gpu_mode != cuttlefish::kGpuModeGuestSwiftshader) {
    return {};
  }
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  return {
      "androidboot.hardware.gralloc=minigbm",
      "androidboot.hardware.hwcomposer=cutf_cvm_ashmem",
      "androidboot.hardware.egl=swiftshader",
      "androidboot.hardware.vulkan=pastel",
  };
}

std::vector<std::string> QemuManager::ConfigureBootDevices() {
  // PCI domain 0, bus 0, device 5, function 0
  return { "androidboot.boot_devices=pci0000:00/0000:00:05.0" };
}

QemuManager::QemuManager(const cuttlefish::CuttlefishConfig* config)
  : VmManager(config) {}

std::vector<cuttlefish::Command> QemuManager::StartCommands() {
  auto instance = config_->ForDefaultInstance();

  auto stop = [](cuttlefish::Subprocess* proc) {
    auto stopped = Stop();
    if (stopped) {
      return true;
    }
    LOG(WARNING) << "Failed to stop VMM nicely, "
                  << "attempting to KILL";
    return KillSubprocess(proc);
  };

  bool is_arm = android::base::EndsWith(config_->qemu_binary(), "system-aarch64");

  cuttlefish::Command qemu_cmd(config_->qemu_binary(), stop);
  qemu_cmd.AddParameter("-name");
  qemu_cmd.AddParameter("guest=", instance.instance_name(), ",debug-threads=on");

  qemu_cmd.AddParameter("-machine");
  auto machine = is_arm ? "virt,gic_version=2" : "pc-i440fx-2.8,accel=kvm";
  qemu_cmd.AddParameter(machine, ",usb=off,dump-guest-core=off");

  qemu_cmd.AddParameter("-m");
  qemu_cmd.AddParameter(config_->memory_mb());

  qemu_cmd.AddParameter("-overcommit");
  qemu_cmd.AddParameter("mem-lock=off");

  qemu_cmd.AddParameter("-smp");
  qemu_cmd.AddParameter(config_->cpus(), ",sockets=", config_->cpus(),
                        ",cores=1,threads=1");

  qemu_cmd.AddParameter("-uuid");
  qemu_cmd.AddParameter(instance.uuid());

  qemu_cmd.AddParameter("-display");
  qemu_cmd.AddParameter("none");

  qemu_cmd.AddParameter("-no-user-config");
  qemu_cmd.AddParameter("-nodefaults");
  qemu_cmd.AddParameter("-no-shutdown");

  qemu_cmd.AddParameter("-rtc");
  qemu_cmd.AddParameter("base=utc");

  qemu_cmd.AddParameter("-boot");
  qemu_cmd.AddParameter("strict=on");

  qemu_cmd.AddParameter("-kernel");
  qemu_cmd.AddParameter(config_->GetKernelImageToUse());

  qemu_cmd.AddParameter("-append");
  qemu_cmd.AddParameter(kernel_cmdline_);

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("socket,id=charmonitor,path=", GetMonitorPath(config_),
                        ",server,nowait");

  qemu_cmd.AddParameter("-mon");
  qemu_cmd.AddParameter("chardev=charmonitor,id=monitor,mode=control");

  // In kgdb mode, earlycon is an interactive console, and so early
  // dmesg will go there instead of the kernel.log
  if (config_->kgdb() || config_->use_bootloader()) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("socket,id=earlycon,path=",
                          instance.console_path(), ",server,nowait");
  } else {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("file,id=earlycon,path=",
                          instance.kernel_log_pipe_name(), ",append=on");
  }

  // On ARM, -serial will imply an AMBA pl011 serial port. On x86, -serial
  // will imply an ISA serial port. We have set up earlycon for each of these
  // port types, so the setting here should match
  qemu_cmd.AddParameter("-serial");
  qemu_cmd.AddParameter("chardev:earlycon");

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

  // In kgdb mode, we have the interactive console on ttyS0 (both Android's
  // console and kdb), so we can disable the virtio-console port usually
  // allocated to Android's serial console, and redirect it to a sink. This
  // ensures that that the PCI device assignments (and thus sepolicy) don't
  // have to change
  if (config_->kgdb() || config_->use_bootloader()) {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("null,id=hvc1");
  } else {
    qemu_cmd.AddParameter("-chardev");
    qemu_cmd.AddParameter("socket,id=hvc1,path=", instance.console_path(),
                          ",server,nowait");
  }

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial1");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtconsole,bus=virtio-serial1.0,chardev=hvc1");

  // If configured, this handles logcat forwarding to the host via serial
  // (instead of vsocket) - /dev/hvc2

  qemu_cmd.AddParameter("-chardev");
  qemu_cmd.AddParameter("file,id=hvc2,path=",
                        instance.logcat_pipe_name(), ",append=on");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-serial-pci-non-transitional,max_ports=1,id=virtio-serial2");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtconsole,bus=virtio-serial2.0,chardev=hvc2");

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

  qemu_cmd.AddParameter("-netdev");
  qemu_cmd.AddParameter("tap,id=hostnet0,ifname=", instance.wifi_tap_name(),
                        ",script=no,downscript=no");

  auto romfile = is_arm ? ",romfile" : "";
  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-net-pci-non-transitional,netdev=hostnet0,id=net0", romfile);

  qemu_cmd.AddParameter("-netdev");
  qemu_cmd.AddParameter("tap,id=hostnet1,ifname=", instance.mobile_tap_name(),
                        ",script=no,downscript=no");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-net-pci-non-transitional,netdev=hostnet1,id=net1", romfile);

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-balloon-pci-non-transitional,id=balloon0");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-gpu-pci,id=gpu0");

  qemu_cmd.AddParameter("-object");
  qemu_cmd.AddParameter("rng-random,id=objrng0,filename=/dev/urandom");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("virtio-rng-pci-non-transitional,rng=objrng0,id=rng0,",
                        "max-bytes=1024,period=2000");

  qemu_cmd.AddParameter("-cpu");
  qemu_cmd.AddParameter(is_arm ? "cortex-a53" : "host");

  qemu_cmd.AddParameter("-msg");
  qemu_cmd.AddParameter("timestamp=on");

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("AC97");

  if (config_->use_bootloader()) {
    qemu_cmd.AddParameter("-bios");
    qemu_cmd.AddParameter(config_->bootloader());
  }

  if (config_->gdb_flag().size() > 0) {
    qemu_cmd.AddParameter("-gdb");
    qemu_cmd.AddParameter(config_->gdb_flag());
  }

  qemu_cmd.AddParameter("-initrd");
  qemu_cmd.AddParameter(config_->final_ramdisk_path());

  qemu_cmd.AddParameter("-device");
  qemu_cmd.AddParameter("vhost-vsock-pci-non-transitional,guest-cid=",
                        instance.vsock_guest_cid());

  LogAndSetEnv("QEMU_AUDIO_DRV", "none");

  std::vector<cuttlefish::Command> ret;
  ret.push_back(std::move(qemu_cmd));
  return ret;
}

} // namespace vm_manager
} // namespace cuttlefish

