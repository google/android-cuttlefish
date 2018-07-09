/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/strings/str_split.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/size_utils.h"
#include "common/vsoc/lib/vsoc_memory.h"
#include "common/vsoc/shm/screen_layout.h"
#include "host/commands/launch/boot_image_unpacker.h"
#include "host/commands/launch/pre_launch_initializers.h"
#include "host/commands/launch/vsoc_shared_memory.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/monitor/kernel_log_server.h"
#include "host/libs/usbip/server.h"
#include "host/libs/vadb/virtual_adb_server.h"
#include "host/libs/vm_manager/vm_manager.h"

using vsoc::GetPerInstanceDefault;

DEFINE_string(
    system_image, "",
    "Path to the system image, if empty it is assumed to be a file named "
    "system.img in the directory specified by -system_image_dir");
DEFINE_string(cache_image, "", "Location of the cache partition image.");
DEFINE_int32(cpus, 2, "Virtual CPU count.");
DEFINE_string(data_image, "", "Location of the data partition image.");
DEFINE_string(data_policy, "use_existing", "How to handle userdata partition."
            " Either 'use_existing', 'create_if_missing', or 'always_create'.");
DEFINE_int32(blank_data_image_mb, 0,
             "The size of the blank data image to generate, MB.");
DEFINE_string(blank_data_image_fmt, "ext4",
              "The fs format for the blank data image. Used with mkfs.");

DEFINE_int32(x_res, 720, "Width of the screen in pixels");
DEFINE_int32(y_res, 1280, "Height of the screen in pixels");
DEFINE_int32(dpi, 160, "Pixels per inch for the screen");
DEFINE_int32(refresh_rate_hz, 60, "Screen refresh rate in Hertz");
DEFINE_int32(num_screen_buffers, 3, "The number of screen buffers");

DEFINE_bool(disable_app_armor_security, false,
            "Disable AppArmor security in libvirt. For debug only.");
DEFINE_bool(disable_dac_security, false,
            "Disable DAC security in libvirt. For debug only.");
DEFINE_string(kernel_path, "",
	      "Path to the kernel. Overrides the one from the boot image");
DEFINE_string(extra_kernel_command_line, "",
              "Additional flags to put on the kernel command line");
DEFINE_string(boot_image, "", "Location of cuttlefish boot image.");
DEFINE_int32(memory_mb, 2048,
             "Total amount of memory available for guest, MB.");
std::string g_default_mempath{GetPerInstanceDefault("/var/run/shm/cvd-")};
DEFINE_string(mempath, g_default_mempath.c_str(),
              "Target location for the shmem file.");
// The cvd-mobile-{tap|br}-xx interfaces are created by default, but libvirt
// needs to create its own on tap interfaces on every run so we use a different
// set for it.
std::string g_default_mobile_interface{
    vsoc::HostSupportsQemuCli() ? GetPerInstanceDefault("cvd-mbr-")
                                : GetPerInstanceDefault("cvd-mobile-")};
DEFINE_string(mobile_interface, g_default_mobile_interface.c_str(),
              "Network interface to use for mobile networking");
std::string g_default_mobile_tap_interface =
    vsoc::HostSupportsQemuCli() ? GetPerInstanceDefault("cvd-mtap-")
                                : GetPerInstanceDefault("amobile");
DEFINE_string(mobile_tap_name, g_default_mobile_tap_interface.c_str(),
              "The name of the tap interface to use for mobile");
std::string g_default_serial_number{GetPerInstanceDefault("CUTTLEFISHCVD")};
DEFINE_string(serial_number, g_default_serial_number.c_str(),
              "Serial number to use for the device");
DEFINE_string(instance_dir, vsoc::GetDefaultPerInstanceDir(),
              "A directory to put all instance specific files");
DEFINE_string(system_image_dir, vsoc::DefaultGuestImagePath(""),
              "Location of the system partition images.");
DEFINE_string(vendor_image, "", "Location of the vendor partition image.");

DEFINE_bool(deprecated_boot_completed, false, "Log boot completed message to"
            " host kernel. This is only used during transition of our clients."
            " Will be deprecated soon.");
DEFINE_bool(start_vnc_server, true, "Whether to start the vnc server process.");
DEFINE_string(vnc_server_binary,
              vsoc::DefaultHostArtifactsPath("bin/vnc_server"),
              "Location of the vnc server binary.");
DEFINE_string(ivserver_binary,
              vsoc::DefaultHostArtifactsPath("bin/ivserver"),
              "Location of the ivshmem server binary.");
DEFINE_int32(vnc_server_port, GetPerInstanceDefault(6444),
             "The port on which the vnc server should listen");
DEFINE_string(socket_forward_proxy_binary,
              vsoc::DefaultHostArtifactsPath("bin/socket_forward_proxy"),
              "Location of the socket_forward_proxy binary.");
DEFINE_string(adb_mode, "tunnel",
              "Mode for adb connection. Can be 'usb' for usb forwarding, "
              "'tunnel' for tcp connection, or a comma separated list of types "
              "as in 'usb,tunnel'");
DEFINE_bool(run_adb_connector, true,
            "Maintain adb connection by sending 'adb connect' commands to the "
            "server. Only relevant with --adb_mode=tunnel");
DEFINE_string(adb_connector_binary,
              vsoc::DefaultHostArtifactsPath("bin/adb_connector"),
              "Location of the adb_connector binary. Only relevant if "
              "--run_adb_connector is true");
DEFINE_int32(vhci_port, GetPerInstanceDefault(0), "VHCI port to use for usb");
DEFINE_string(guest_mac_address,
              GetPerInstanceDefault("00:43:56:44:80:"), // 00:43:56:44:80:0x
              "MAC address of the wifi interface to be created on the guest.");
DEFINE_string(host_mac_address,
              "42:00:00:00:00:00",
              "MAC address of the wifi interface running on the host.");
DEFINE_string(wifi_interface,
              vsoc::HostSupportsQemuCli() ? GetPerInstanceDefault("cvd-wbr-")
                                          : GetPerInstanceDefault("cvd-wifi-"),
              "Network interface to use for wifi");
DEFINE_string(wifi_tap_name,
              vsoc::HostSupportsQemuCli() ? GetPerInstanceDefault("cvd-wtap-")
                                          : GetPerInstanceDefault("awifi"),
              "The name of the tap interface to use for wifi");
// TODO(b/72969289) This should be generated
DEFINE_string(dtb, "", "Path to the cuttlefish.dtb file");

constexpr char kDefaultUuidPrefix[] = "699acfc4-c8c4-11e7-882b-5065f31dc1";
DEFINE_string(uuid, vsoc::GetPerInstanceDefault(kDefaultUuidPrefix).c_str(),
              "UUID to use for the device. Random if not specified");

DECLARE_string(config_file);

namespace {
const std::string kDataPolicyUseExisting = "use_existing";
const std::string kDataPolicyCreateIfMissing = "create_if_missing";
const std::string kDataPolicyAlwaysCreate = "always_create";

constexpr char kAdbModeTunnel[] = "tunnel";
constexpr char kAdbModeUsb[] = "usb";

// VirtualUSBManager manages virtual USB device presence for Cuttlefish.
class VirtualUSBManager {
 public:
  VirtualUSBManager(const std::string& usbsocket, int vhci_port,
                    const std::string& android_usbipsocket)
      : adb_{usbsocket, vhci_port, android_usbipsocket},
        usbip_{android_usbipsocket, adb_.Pool()} {}

  ~VirtualUSBManager() = default;

  // Initialize Virtual USB and start USB management thread.
  void Start() {
    CHECK(adb_.Init()) << "Could not initialize Virtual ADB server";
    CHECK(usbip_.Init()) << "Could not start USB/IP server";
    std::thread([this] { Thread(); }).detach();
  }

 private:
  void Thread() {
    for (;;) {
      cvd::SharedFDSet fd_read;
      fd_read.Zero();

      adb_.BeforeSelect(&fd_read);
      usbip_.BeforeSelect(&fd_read);

      int ret = cvd::Select(&fd_read, nullptr, nullptr, nullptr);
      if (ret <= 0) continue;

      adb_.AfterSelect(fd_read);
      usbip_.AfterSelect(fd_read);
    }
  }

  vadb::VirtualADBServer adb_;
  vadb::usbip::Server usbip_;

  VirtualUSBManager(const VirtualUSBManager&) = delete;
  VirtualUSBManager& operator=(const VirtualUSBManager&) = delete;
};

// KernelLogMonitor receives and monitors kernel log for Cuttlefish.
class KernelLogMonitor {
 public:
  KernelLogMonitor(const std::string& socket_name,
                   const std::string& log_name,
                   bool deprecated_boot_completed)
      : klog_{socket_name, log_name, deprecated_boot_completed} {}

  ~KernelLogMonitor() = default;

  void Start() {
    CHECK(klog_.Init()) << "Could not initialize kernel log server";
    std::thread([this] { Thread(); }).detach();
  }

 private:
  void Thread() {
    for (;;) {
      cvd::SharedFDSet fd_read;
      fd_read.Zero();

      klog_.BeforeSelect(&fd_read);

      int ret = cvd::Select(&fd_read, nullptr, nullptr, nullptr);
      if (ret <= 0) continue;

      klog_.AfterSelect(fd_read);
    }
  }

  monitor::KernelLogServer klog_;

  KernelLogMonitor(const KernelLogMonitor&) = delete;
  KernelLogMonitor& operator=(const KernelLogMonitor&) = delete;
};

void CreateBlankImage(
    const std::string& image, int image_mb, const std::string& image_fmt) {
  LOG(INFO) << "Creating " << image;
  std::string of = "of=";
  of += image;
  std::string count = "count=";
  count += std::to_string(image_mb);
  cvd::execute({"/bin/dd", "if=/dev/zero", of, "bs=1M", count});
  cvd::execute({"/sbin/mkfs", "-t", image_fmt, image}, {"PATH=/sbin"});
}

void RemoveFile(const std::string& file) {
  LOG(INFO) << "Removing " << file;
  cvd::execute({"/bin/rm", "-f", file});
}

bool ApplyDataImagePolicy(const char* data_image) {
  bool data_exists = cvd::FileHasContent(data_image);
  bool remove{};
  bool create{};

  if (FLAGS_data_policy == kDataPolicyUseExisting) {
    if (!data_exists) {
      LOG(FATAL) << "Specified data image file does not exists: " << data_image;
      return false;
    }
    if (FLAGS_blank_data_image_mb > 0) {
      LOG(FATAL) << "You should NOT use -blank_data_image_mb with -data_policy="
                 << kDataPolicyUseExisting;
      return false;
    }
    create = false;
    remove = false;
  } else if (FLAGS_data_policy == kDataPolicyAlwaysCreate) {
    remove = data_exists;
    create = true;
  } else if (FLAGS_data_policy == kDataPolicyCreateIfMissing) {
    create = !data_exists;
    remove = false;
  } else {
    LOG(FATAL) << "Invalid data_policy: " << FLAGS_data_policy;
  }

  if (remove) {
    RemoveFile(data_image);
  }

  if (create) {
    if (FLAGS_blank_data_image_mb <= 0) {
      LOG(FATAL) << "-blank_data_image_mb is required to create data image";
    }
    CreateBlankImage(
        data_image, FLAGS_blank_data_image_mb, FLAGS_blank_data_image_fmt);
  } else {
    LOG(INFO) << data_image << " exists. Not creating it.";
  }

  return true;
}

std::string GetConfigFile() {
  return vsoc::CuttlefishConfig::Get()->PerInstancePath(
      "cuttlefish_config.json");
}

std::string GetConfigFileArg() { return "-config_file=" + GetConfigFile(); }

std::string GetGuestPortArg() {
  constexpr int kEmulatorPort = 5555;
  return std::string{"--guest_ports="} + std::to_string(kEmulatorPort);
}

int GetHostPort() {
  constexpr int kFirstHostPort = 6520;
  return vsoc::GetPerInstanceDefault(kFirstHostPort);
}

std::string GetHostPortArg() {
  return std::string{"--host_ports="} + std::to_string(GetHostPort());
}

std::string GetAdbConnectorPortArg() {
  return std::string{"--ports="} + std::to_string(GetHostPort());
}

bool AdbModeEnabled(const char* mode) {
  auto modes = cvd::StrSplit(FLAGS_adb_mode, ',');
  return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

bool AdbTunnelEnabled() {
  return AdbModeEnabled(kAdbModeTunnel);
}

bool AdbUsbEnabled() {
  return AdbModeEnabled(kAdbModeUsb);
}

void ValidateAdbModeFlag() {
  if (!AdbUsbEnabled() && !AdbTunnelEnabled()) {
    LOG(INFO) << "ADB not enabled";
  }
}

int CreateIvServerUnixSocket(const std::string& path) {
  return cvd::SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM,
                                          0666)->UNMANAGED_Dup();
}

bool AdbConnectorEnabled() {
  return FLAGS_run_adb_connector;
}

void LaunchIvServer() {
  auto config = vsoc::CuttlefishConfig::Get();
  // Resize gralloc region
  auto actual_width = cvd::AlignToPowerOf2(FLAGS_x_res * 4, 4);  // align to 16
  uint32_t screen_buffers_size =
      FLAGS_num_screen_buffers *
      cvd::AlignToPageSize(actual_width * FLAGS_y_res + 16 /* padding */);
  screen_buffers_size +=
      (FLAGS_num_screen_buffers - 1) * 4096; /* Guard pages */

  // TODO(b/79170615) Resize gralloc region too.

  vsoc::CreateSharedMemoryFile(
      config->mempath(),
      {{vsoc::layout::screen::ScreenLayout::region_name, screen_buffers_size}});

  auto qemu_channel =
      CreateIvServerUnixSocket(config->ivshmem_qemu_socket_path());
  auto client_channel =
      CreateIvServerUnixSocket(config->ivshmem_client_socket_path());
  auto qemu_socket_arg = "-qemu_socket_fd=" + std::to_string(qemu_channel);
  auto client_socket_arg =
      "-client_socket_fd=" + std::to_string(client_channel);
  cvd::subprocess({FLAGS_ivserver_binary, qemu_socket_arg, client_socket_arg,
                   GetConfigFileArg()});
  close(qemu_channel);
  close(client_channel);
}

void LaunchAdbConnectorIfEnabled() {
  if (AdbConnectorEnabled()) {
    cvd::subprocess({FLAGS_adb_connector_binary,
                     GetAdbConnectorPortArg()});
  }
}

void LaunchSocketForwardProxyIfEnabled() {
  if (AdbTunnelEnabled()) {
    cvd::subprocess({FLAGS_socket_forward_proxy_binary,
                     GetGuestPortArg(),
                     GetHostPortArg(),
                     GetConfigFileArg()});
  }
}

void LaunchVNCServerIfEnabled() {
  if (FLAGS_start_vnc_server) {
    // Launch the vnc server, don't wait for it to complete
    auto port_options = "-port=" + std::to_string(FLAGS_vnc_server_port);
    cvd::subprocess(
        {FLAGS_vnc_server_binary, port_options, GetConfigFileArg()});
  }
}

bool ResolveInstanceFiles() {
  if (FLAGS_system_image_dir.empty()) {
    LOG(FATAL) << "--system_image_dir must be specified.";
    return false;
  }

  // If user did not specify location of either of these files, expect them to
  // be placed in --system_image_dir location.
  if (FLAGS_system_image.empty()) {
    FLAGS_system_image = FLAGS_system_image_dir + "/system.img";
  }
  if (FLAGS_boot_image.empty()) {
    FLAGS_boot_image = FLAGS_system_image_dir + "/boot.img";
  }
  if (FLAGS_cache_image.empty()) {
    FLAGS_cache_image = FLAGS_system_image_dir + "/cache.img";
  }
  if (FLAGS_data_image.empty()) {
    FLAGS_data_image = FLAGS_system_image_dir + "/userdata.img";
  }
  if (FLAGS_vendor_image.empty()) {
    FLAGS_vendor_image = FLAGS_system_image_dir + "/vendor.img";
  }

  // Create data if necessary
  if (!ApplyDataImagePolicy(FLAGS_data_image.c_str())) {
    return false;
  }

  // Check that the files exist
  for (const auto& file :
       {FLAGS_system_image, FLAGS_vendor_image, FLAGS_cache_image,
        FLAGS_data_image, FLAGS_boot_image}) {
    if (!cvd::FileHasContent(file.c_str())) {
      LOG(FATAL) << "File not found: " << file;
      return false;
    }
  }
  return true;
}

bool UnpackBootImage(const cvd::BootImageUnpacker& boot_image_unpacker) {
  auto config = vsoc::CuttlefishConfig::Get();
  if (boot_image_unpacker.HasRamdiskImage()) {
    if (!boot_image_unpacker.ExtractRamdiskImage(config->ramdisk_image_path())) {
      LOG(FATAL) << "Error extracting ramdisk from boot image";
      return false;
    }
  }
  if (!FLAGS_kernel_path.size()) {
    if (boot_image_unpacker.HasKernelImage()) {
      if (!boot_image_unpacker.ExtractKernelImage(config->kernel_image_path())) {
        LOG(ERROR) << "Error extracting kernel from boot image";
        return false;
      }
    } else {
      LOG(ERROR) << "No kernel found on boot image";
      return false;
    }
  }
  return true;
}

bool SetUpGlobalConfiguration(
    const cvd::BootImageUnpacker& boot_image_unpacker) {
  auto& memory_layout = *vsoc::VSoCMemoryLayout::Get();
  auto config = vsoc::CuttlefishConfig::Get();
  // Set this first so that calls to PerInstancePath below are correct
  config->set_instance_dir(FLAGS_instance_dir);

  config->set_serial_number(FLAGS_serial_number);

  config->set_cpus(FLAGS_cpus);
  config->set_memory_mb(FLAGS_memory_mb);

  config->set_dpi(FLAGS_dpi);
  config->set_x_res(FLAGS_x_res);
  config->set_y_res(FLAGS_y_res);
  config->set_refresh_rate_hz(FLAGS_refresh_rate_hz);

  if (FLAGS_kernel_path.size()) {
    config->set_kernel_image_path(FLAGS_kernel_path);
  } else {
    config->set_kernel_image_path(config->PerInstancePath("kernel"));
  }

  auto ramdisk_path = config->PerInstancePath("ramdisk.img");
  bool use_ramdisk = boot_image_unpacker.HasRamdiskImage();
  if (!use_ramdisk) {
    LOG(INFO) << "No ramdisk present; assuming system-as-root build";
    ramdisk_path = "";
  }

  // This needs to be done here because the dtb path depends on the presence of
  // the ramdisk
  if (FLAGS_dtb.empty()) {
    if (use_ramdisk) {
      FLAGS_dtb = vsoc::DefaultHostArtifactsPath("config/initrd-root.dtb");
    } else {
      FLAGS_dtb = vsoc::DefaultHostArtifactsPath("config/system-root.dtb");
    }
  }

  std::ostringstream kernel_cmdline;
  kernel_cmdline << boot_image_unpacker.kernel_command_line();
  if (!use_ramdisk) {
    kernel_cmdline << " root=/dev/vda init=/init";
  }
  kernel_cmdline << " androidboot.serialno=" << FLAGS_serial_number;
  kernel_cmdline << " androidboot.lcd_density=" << FLAGS_dpi;
  if (FLAGS_extra_kernel_command_line.size()) {
    kernel_cmdline << " " << FLAGS_extra_kernel_command_line;
  }
  config->set_kernel_args(kernel_cmdline.str());

  config->set_ramdisk_image_path(ramdisk_path);
  config->set_system_image_path(FLAGS_system_image);
  config->set_cache_image_path(FLAGS_cache_image);
  config->set_data_image_path(FLAGS_data_image);
  config->set_vendor_image_path(FLAGS_vendor_image);
  config->set_dtb_path(FLAGS_dtb);

  config->set_mempath(FLAGS_mempath);
  config->set_ivshmem_qemu_socket_path(
      config->PerInstancePath("ivshmem_socket_qemu"));
  config->set_ivshmem_client_socket_path(
      config->PerInstancePath("ivshmem_socket_client"));
  config->set_ivshmem_vector_count(memory_layout.GetRegions().size());

  config->set_usb_v1_socket_name(config->PerInstancePath("usb-v1"));
  config->set_vhci_port(FLAGS_vhci_port);
  config->set_usb_ip_socket_name(config->PerInstancePath("usb-ip"));

  config->set_kernel_log_socket_name(config->PerInstancePath("kernel-log"));
  config->set_console_path(config->PerInstancePath("console"));
  config->set_logcat_path(config->PerInstancePath("logcat"));

  config->set_mobile_bridge_name(FLAGS_mobile_interface);
  config->set_mobile_tap_name(FLAGS_mobile_tap_name);

  config->set_wifi_bridge_name(FLAGS_wifi_interface);
  config->set_wifi_tap_name(FLAGS_wifi_tap_name);

  config->set_wifi_guest_mac_addr(FLAGS_guest_mac_address);
  config->set_wifi_host_mac_addr(FLAGS_host_mac_address);

  config->set_entropy_source("/dev/urandom");
  config->set_uuid(FLAGS_uuid);

  config->set_disable_dac_security(FLAGS_disable_dac_security);
  config->set_disable_app_armor_security(FLAGS_disable_app_armor_security);

  if(!AdbUsbEnabled()) {
    config->disable_usb_adb();
  }

  config->set_cuttlefish_env_path(cvd::StringFromEnv("HOME", ".") +
                                  "/.cuttlefish.sh");

  return true;
}

bool ParseCommandLineFlags(int argc, char** argv) {
  // The config_file is created by the launcher, so the launcher is the only
  // host process that doesn't use the flag.
  // Set the default to empty.
  google::SetCommandLineOptionWithMode("config_file", "",
                                       gflags::SET_FLAGS_DEFAULT);
  google::ParseCommandLineFlags(&argc, &argv, true);
  // Set the flag value to empty (in case the caller passed a value for it).
  FLAGS_config_file = "";

  ValidateAdbModeFlag();

  return ResolveInstanceFiles();
}

bool WriteCuttlefishEnvironment() {
  auto config = vsoc::CuttlefishConfig::Get();
  auto env = cvd::SharedFD::Open(config->cuttlefish_env_path().c_str(),
                                 O_CREAT | O_RDWR, 0755);
  if (!env->IsOpen()) {
    LOG(ERROR) << "Unable to create cuttlefish.env file";
    return false;
  }
  std::string config_env = "export CUTTLEFISH_PER_INSTANCE_PATH=\"" +
                           config->PerInstancePath(".") + "\"\n";
  config_env += "export ANDROID_SERIAL=";
  if (AdbUsbEnabled()) {
    config_env += config->serial_number();
  } else {
    config_env += "127.0.0.1:" + std::to_string(GetHostPort());
  }
  config_env += "\n";
  env->Write(config_env.c_str(), config_env.size());
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  if (!ParseCommandLineFlags(argc, argv)) {
    return -1;
  }

  auto boot_img_unpacker = cvd::BootImageUnpacker::FromImage(FLAGS_boot_image);
  auto vm_manager = vm_manager::VmManager::Get();

  // Do this early so that the config object is ready for anything that needs it
  if (!SetUpGlobalConfiguration(*boot_img_unpacker)) {
    return -1;
  }

  if (!vm_manager->EnsureInstanceDirExists()) {
    LOG(ERROR) << "Failed to create instance directory: " << FLAGS_instance_dir;
    return -1;
  }

  if (!vm_manager->CleanPriorFiles()) {
    LOG(ERROR) << "Failed to clean prior files";
    return -1;
  }

  if (!UnpackBootImage(*boot_img_unpacker)) {
    LOG(ERROR) << "Failed to unpack boot image";
    return -1;
  }

  if (!WriteCuttlefishEnvironment()) {
    LOG(ERROR) << "Unable to write cuttlefish environment file";
  }

  auto config = vsoc::CuttlefishConfig::Get();
  // Save the config object before starting any host process
  if (!config->SaveToFile(GetConfigFile())) {
    return -1;
  }

  // Start the usb manager
  VirtualUSBManager vadb(config->usb_v1_socket_name(), config->vhci_port(),
                         config->usb_ip_socket_name());
  vadb.Start();

  LaunchIvServer();

  KernelLogMonitor kmon(config->kernel_log_socket_name(),
                        config->PerInstancePath("kernel.log"),
                        FLAGS_deprecated_boot_completed);
  kmon.Start();

  // Initialize the regions that require so before the VM starts.
  PreLaunchInitializers::Initialize();

  // Start the guest VM
  if (!vm_manager->Start()) {
    LOG(FATAL) << "Unable to start vm_manager";
    return -1;
  }

  LaunchSocketForwardProxyIfEnabled();
  LaunchVNCServerIfEnabled();
  LaunchAdbConnectorIfEnabled();

  pause();
}
