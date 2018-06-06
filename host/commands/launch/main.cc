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

#include "common/libs/fs/shared_select.h"
#include "common/libs/strings/str_split.h"
#include "common/vsoc/lib/vsoc_memory.h"
#include "common/vsoc/shm/screen_layout.h"
#include "host/commands/launch/pre_launch_initializers.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/ivserver/ivserver.h"
#include "host/libs/ivserver/options.h"
#include "host/libs/monitor/kernel_log_server.h"
#include "host/libs/usbip/server.h"
#include "host/libs/vadb/virtual_adb_server.h"
#include "host/libs/vm_manager/libvirt_manager.h"

namespace {
std::string StringFromEnv(const char* varname, std::string defval) {
  const char* const valstr = getenv(varname);
  if (!valstr) {
    return defval;
  }
  return valstr;
}

std::string DefaultHostArtifactsPath(const char* file_name) {
  return (StringFromEnv("ANDROID_HOST_OUT", StringFromEnv("HOME", ".")) + "/") +
         file_name;
}

}  // namespace

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
DEFINE_string(extra_kernel_command_line, "",
              "Additional flags to put on the kernel command line");
DEFINE_string(initrd, "", "Location of cuttlefish initrd file.");
DEFINE_string(kernel, "", "Location of cuttlefish kernel file.");
DEFINE_string(kernel_command_line, "",
              "Location of a text file with the kernel command line.");
DEFINE_int32(memory_mb, 2048,
             "Total amount of memory available for guest, MB.");
std::string g_default_mempath{GetPerInstanceDefault("/var/run/shm/cvd-")};
DEFINE_string(mempath, g_default_mempath.c_str(),
              "Target location for the shmem file.");
std::string g_default_mobile_interface{GetPerInstanceDefault("cvd-mobile-")};
DEFINE_string(mobile_interface, g_default_mobile_interface.c_str(),
              "Network interface to use for mobile networking");
DEFINE_string(mobile_tap_name, GetPerInstanceDefault("amobile"),
              "The name of the tap interface to use for mobile");
std::string g_default_serial_number{GetPerInstanceDefault("CUTTLEFISHCVD")};
DEFINE_string(serial_number, g_default_serial_number.c_str(),
              "Serial number to use for the device");
DEFINE_string(instance_dir, vsoc::GetDefaultPerInstanceDir(),
              "A directory to put all instance specific files");
DEFINE_string(system_image_dir,
              StringFromEnv("ANDROID_PRODUCT_OUT", StringFromEnv("HOME", ".")),
              "Location of the system partition images.");
DEFINE_string(vendor_image, "", "Location of the vendor partition image.");

DEFINE_bool(deprecated_boot_completed, false, "Log boot completed message to"
            " host kernel. This is only used during transition of our clients."
            " Will be deprecated soon.");
DEFINE_bool(start_vnc_server, true, "Whether to start the vnc server process.");
DEFINE_string(vnc_server_binary,
              DefaultHostArtifactsPath("/bin/vnc_server"),
              "Location of the vnc server binary.");
DEFINE_int32(vnc_server_port, GetPerInstanceDefault(6444),
             "The port on which the vnc server should listen");
DEFINE_string(socket_forward_proxy_binary,
              DefaultHostArtifactsPath("/bin/socket_forward_proxy"),
              "Location of the socket_forward_proxy binary.");
DEFINE_string(adb_mode, "tunnel",
              "Mode for adb connection. Can be usb for usb forwarding, or "
              "tunnel for tcp connection. If using tunnel, you may have to "
              "run 'adb kill-server' to get the device to show up.");
DEFINE_int32(vhci_port, GetPerInstanceDefault(0), "VHCI port to use for usb");
DEFINE_string(guest_mac_address,
              GetPerInstanceDefault("00:43:56:44:80:"), // 00:43:56:44:80:0x
              "MAC address of the wifi interface to be created on the guest.");
DEFINE_string(host_mac_address,
              "42:00:00:00:00:00",
              "MAC address of the wifi interface running on the host.");
DEFINE_bool(start_wifi_relay, true, "Whether to start the wifi_relay process.");
DEFINE_string(wifi_relay_binary,
              DefaultHostArtifactsPath("/bin/wifi_relay"),
              "Location of the wifi_relay binary.");
std::string g_default_wifi_interface{GetPerInstanceDefault("cvd-wifi-")};
DEFINE_string(wifi_interface, g_default_wifi_interface.c_str(),
              "Network interface to use for wifi");
// TODO(b/72969289) This should be generated
DEFINE_string(dtb, DefaultHostArtifactsPath("config/cuttlefish.dtb"),
              "Path to the cuttlefish.dtb file");

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

// IVServerManager takes care of serving shared memory segments between
// Cuttlefish and host-side daemons.
class IVServerManager {
 public:
  IVServerManager(const std::string& mempath, const std::string& qemu_socket)
      : server_(ivserver::IVServerOptions(mempath, qemu_socket,
                                          vsoc::GetDomain())) {}

  ~IVServerManager() = default;

  // Start IVServer thread.
  void Start() {
    std::thread([this] { server_.Serve(); }).detach();
  }

 private:
  ivserver::IVServer server_;

  IVServerManager(const IVServerManager&) = delete;
  IVServerManager& operator=(const IVServerManager&) = delete;
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

void subprocess(const char* const* command,
                const char* const* envp,
                bool wait_for_child = true) {
  pid_t pid = fork();
  if (!pid) {
    int rval;
    // If envp is NULL, the current process's environment is used as the
    // environment of the child process. To force an empty emvironment for the
    // child process pass the address of a pointer to NULL
    if (envp == NULL) {
      rval = execv(command[0], const_cast<char* const*>(command));
    } else {
      rval = execve(command[0], const_cast<char* const*>(command),
                    const_cast<char* const*>(envp));
    }
    // No need for an if: if exec worked it wouldn't have returned
    LOG(ERROR) << "exec of " << command[0] << " failed (" << strerror(errno)
               << ")";
    exit(rval);
  }
  if (pid == -1) {
    LOG(ERROR) << "fork of " << command[0] << " failed (" << strerror(errno)
               << ")";
  }
  if (pid > 0) {
    if (wait_for_child) {
      waitpid(pid, 0, 0);
    } else {
      LOG(INFO) << "Started " << command[0] << ", pid: " << pid;
    }
  }
}

bool FileExists(const char* path) {
  struct stat unused;
  return stat(path, &unused) != -1 || errno != ENOENT;
}

void CreateBlankImage(
    const std::string& image, int image_mb, const std::string& image_fmt) {
  LOG(INFO) << "Creating " << image;
  std::string of = "of=";
  of += image;
  std::string count = "count=";
  count += std::to_string(image_mb);
  const char* dd_command[]{
    "/bin/dd", "if=/dev/zero", of.c_str(), "bs=1M", count.c_str(), NULL};
  subprocess(dd_command, NULL);
  const char* mkfs_command[]{
    "/sbin/mkfs", "-t", image_fmt.c_str(), image.c_str(), NULL};
  const char* envp[]{"PATH=/sbin", NULL};
  subprocess(mkfs_command, envp);
}

void RemoveFile(const std::string& file) {
  LOG(INFO) << "Removing " << file;
  const char* rm_command[]{
    "/bin/rm", "-f", file.c_str(), NULL};
  subprocess(rm_command, NULL);
}

bool ApplyDataImagePolicy(const char* data_image) {
  bool data_exists = FileExists(data_image);
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

bool EnsureDirExists(const char* dir) {
  if (!FileExists(dir)) {
    LOG(INFO) << "Setting up " << dir;
    if (mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
      if (errno == EACCES) {
        // TODO(79170615) Don't use sudo once libvirt is replaced
        LOG(WARNING) << "Not enough permission to create " << dir
                     << " retrying with sudo";
        const char* mkdir_command[]{"/usr/bin/sudo", "/bin/mkdir", "-m",
                                    "0775",          dir,          NULL};
        subprocess(mkdir_command, NULL);

        // When created with sudo the owner and group is root.
        std::string user_group = getenv("USER");
        user_group += ":libvirt-qemu";
        const char* chown_cmd[] = {"/usr/bin/sudo", "/bin/chown",
                                   user_group.c_str(), dir, NULL};
        subprocess(chown_cmd, NULL);
      } else {
        LOG(FATAL) << "Unable to create " << dir << ". Error: " << errno;
        return false;
      }
    }
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

std::string GetHostPortArg() {
  constexpr int kFirstHostPort = 6520;
  return std::string{"--host_ports="} +
      std::to_string(vsoc::GetPerInstanceDefault(kFirstHostPort));
}

void ValidateAdbModeFlag() {
  CHECK(FLAGS_adb_mode == kAdbModeUsb ||
        FLAGS_adb_mode == kAdbModeTunnel) << "invalid --adb_mode";
}

bool AdbTunnelEnabled() {
  return FLAGS_adb_mode == kAdbModeTunnel;
}

bool AdbUsbEnabled() {
  return FLAGS_adb_mode == kAdbModeUsb;
}

void LaunchSocketForwardProxyIfEnabled() {
  if (AdbTunnelEnabled()) {
    auto guest_port_arg = GetGuestPortArg();
    auto host_port_arg = GetHostPortArg();
    auto config_arg = GetConfigFileArg();

    const char* const socket_proxy[] = {
      FLAGS_socket_forward_proxy_binary.c_str(),
      guest_port_arg.c_str(),
      host_port_arg.c_str(),
      config_arg.c_str(),
      NULL
    };
    subprocess(socket_proxy, nullptr, false);
  }
}

void LaunchVNCServerIfEnabled() {
  if (FLAGS_start_vnc_server) {
    // Launch the vnc server, don't wait for it to complete
    auto port_options = "-port=" + std::to_string(FLAGS_vnc_server_port);
    auto config_arg = GetConfigFileArg();
    const char* vnc_command[] = {
      FLAGS_vnc_server_binary.c_str(),
      port_options.c_str(),
      config_arg.c_str(),
      NULL
    };
    subprocess(vnc_command, NULL, false);
  }
}

void LaunchWifiRelayIfEnabled() {
  if (FLAGS_start_wifi_relay) {
    // Launch the wifi relay, don't wait for it to complete
    auto config_arg = GetConfigFileArg();
    const char* relay_command[] = {
        "/usr/bin/sudo",
        "-E",
        FLAGS_wifi_relay_binary.c_str(),
        config_arg.c_str(),
        NULL
    };

    subprocess(relay_command, NULL /* envp */, false /* wait_for_child */);
  }
}
bool ResolveInstanceFiles() {
  if (FLAGS_system_image_dir.empty()) {
    LOG(FATAL) << "--system_image_dir must be specified.";
    return false;
  }

  // If user did not specify location of either of these files, expect them to
  // be placed in --system_image_dir location.
  if (FLAGS_kernel.empty()) {
    FLAGS_kernel = FLAGS_system_image_dir + "/kernel";
  }
  if (FLAGS_kernel_command_line.empty()) {
    FLAGS_kernel_command_line = FLAGS_system_image_dir + "/cmdline";
  }
  if (FLAGS_system_image.empty()) {
    FLAGS_system_image = FLAGS_system_image_dir + "/system.img";
  }
  if (FLAGS_initrd.empty()) {
    FLAGS_initrd = FLAGS_system_image_dir + "/ramdisk.img";
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
       {FLAGS_system_image, FLAGS_vendor_image, FLAGS_cache_image, FLAGS_kernel,
        FLAGS_initrd, FLAGS_data_image, FLAGS_kernel_command_line}) {
    if (!FileExists(file.c_str())) {
      LOG(FATAL) << "File not found: " << file;
      return false;
    }
  }
  return true;
}

bool SetUpGlobalConfiguration() {
  if (!ResolveInstanceFiles()) {
    return false;
  }
  auto& memory_layout = *vsoc::VSoCMemoryLayout::Get();
  auto config = vsoc::CuttlefishConfig::Get();
  // Set this first so that calls to PerInstancePath below are correct
  config->set_instance_dir(FLAGS_instance_dir);
  if (!EnsureDirExists(FLAGS_instance_dir.c_str())) {
    return false;
  }

  config->set_serial_number(FLAGS_serial_number);

  config->set_cpus(FLAGS_cpus);
  config->set_memory_mb(FLAGS_memory_mb);

  config->set_dpi(FLAGS_dpi);
  config->set_x_res(FLAGS_x_res);
  config->set_y_res(FLAGS_y_res);
  config->set_refresh_rate_hz(FLAGS_refresh_rate_hz);

  config->set_kernel_image_path(FLAGS_kernel);
  std::ostringstream extra_cmdline;
  extra_cmdline << " androidboot.serialno=" << FLAGS_serial_number;
  extra_cmdline << " androidboot.lcd_density=" << FLAGS_dpi;
  if (FLAGS_extra_kernel_command_line.size()) {
    extra_cmdline << " " << FLAGS_extra_kernel_command_line;
  }
  config->ReadKernelArgs(FLAGS_kernel_command_line.c_str(),
                         extra_cmdline.str());

  config->set_ramdisk_image_path(FLAGS_initrd);
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

  config->set_wifi_guest_mac_addr(FLAGS_guest_mac_address);
  config->set_wifi_host_mac_addr(FLAGS_host_mac_address);

  config->set_entropy_source("/dev/urandom");
  config->set_uuid(FLAGS_uuid);

  config->set_disable_dac_security(FLAGS_disable_dac_security);
  config->set_disable_app_armor_security(FLAGS_disable_app_armor_security);

  if(!AdbUsbEnabled()) {
    config->disable_usb_adb();
  }

  return true;
}

}  // anonymous namespace

namespace launch_cvd {
void ParseCommandLineFlags(int argc, char** argv) {
  // The config_file is created by the launcher, so the launcher is the only
  // host process that doesn't use the flag.
  // Set the default to empty.
  google::SetCommandLineOptionWithMode("config_file", "",
                                       gflags::SET_FLAGS_DEFAULT);
  google::ParseCommandLineFlags(&argc, &argv, true);
  // Set the flag value to empty (in case the caller passed a value for it).
  FLAGS_config_file = "";

  ValidateAdbModeFlag();
}
}  // namespace launch_cvd

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  launch_cvd::ParseCommandLineFlags(argc, argv);

  // Do this early so that the config object is ready for anything that needs it
  if (!SetUpGlobalConfiguration()) {
    return -1;
  }

  auto& memory_layout = *vsoc::VSoCMemoryLayout::Get();
  // TODO(b/79170615) These values need to go to the config object/file and the
  // region resizing be done by the ivserver process (or maybe the config
  // library to ensure all processes have the correct value?)
  size_t screen_region_size =
      memory_layout
          .GetRegionByName(vsoc::layout::screen::ScreenLayout::region_name)
          ->region_size();
  auto actual_width = ((FLAGS_x_res * 4) + 15) & ~15;  // aligned to 16
  screen_region_size += FLAGS_num_screen_buffers *
                 (actual_width * FLAGS_y_res + 16 /* padding */);
  screen_region_size += (FLAGS_num_screen_buffers - 1) * 4096; /* Guard pages */
  memory_layout.ResizeRegion(vsoc::layout::screen::ScreenLayout::region_name,
                             screen_region_size);
  // TODO(b/79170615) Resize gralloc region too.


  auto config = vsoc::CuttlefishConfig::Get();
  // Save the config object before starting any host process
  if (!config->SaveToFile(GetConfigFile())) {
    return -1;
  }

  // Start the usb manager
  VirtualUSBManager vadb(config->usb_v1_socket_name(), config->vhci_port(),
                         config->usb_ip_socket_name());
  vadb.Start();

  // Start IVServer
  IVServerManager ivshmem(config->mempath(), config->ivshmem_qemu_socket_path());
  ivshmem.Start();

  KernelLogMonitor kmon(config->kernel_log_socket_name(),
                        config->PerInstancePath("kernel.log"),
                        FLAGS_deprecated_boot_completed);
  kmon.Start();

  // Initialize the regions that require so before the VM starts.
  PreLaunchInitializers::Initialize();

  // Start the guest VM
  vm_manager::LibvirtManager libvirt;
  if (!libvirt.Start()) {
    LOG(FATAL) << "Unable to start libvirt";
    return -1;
  }

  LaunchSocketForwardProxyIfEnabled();
  LaunchVNCServerIfEnabled();
  LaunchWifiRelayIfEnabled();

  pause();
}
