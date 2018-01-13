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

#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_select.h"
#include "host/commands/launch/pre_launch_initializers.h"
#include "host/libs/config/file_partition.h"
#include "host/libs/config/guest_config.h"
#include "host/libs/config/host_config.h"
#include "host/libs/ivserver/ivserver.h"
#include "host/libs/ivserver/options.h"
#include "host/libs/monitor/kernel_log_server.h"
#include "host/libs/usbip/server.h"
#include "host/libs/vadb/virtual_adb_server.h"

namespace {
std::string StringFromEnv(const char* varname, std::string defval) {
  const char* const valstr = getenv(varname);
  if (!valstr) {
    return defval;
  }
  return valstr;
}
}  // namespace

using vsoc::GetPerInstanceDefault;
using vsoc::GetDefaultPerInstancePath;

DEFINE_string(cache_image, "", "Location of the cache partition image.");
DEFINE_int32(cpus, 2, "Virtual CPU count.");
DEFINE_string(data_image, "", "Location of the data partition image.");
DEFINE_string(data_policy, "use_existing", "How to handle userdata partition."
            " Either 'use_existing', 'create_if_missing', or 'always_create'.");
DEFINE_int32(blank_data_image_mb, 0,
             "The size of the blank data image to generate, MB.");
DEFINE_string(blank_data_image_fmt, "ext4",
              "The fs format for the blank data image. Used with mkfs.");
DEFINE_bool(disable_app_armor_security, false,
            "Disable AppArmor security in libvirt. For debug only.");
DEFINE_bool(disable_dac_security, false,
            "Disable DAC security in libvirt. For debug only.");
DEFINE_string(extra_kernel_command_line, "",
              "Additional flags to put on the kernel command line");
DECLARE_int32(instance);
DEFINE_string(initrd, "", "Location of cuttlefish initrd file.");
DEFINE_string(kernel, "", "Location of cuttlefish kernel file.");
DEFINE_string(kernel_command_line, "",
              "Location of a text file with the kernel command line.");
DEFINE_string(launch_command, "virsh create /dev/fd/0",
              "Command to start an instance");
DEFINE_string(layout,
              StringFromEnv("ANDROID_HOST_OUT", StringFromEnv("HOME", ".")) +
                  "/config/vsoc_mem.json",
              "Location of the vsoc_mem.json file.");
DEFINE_bool(log_xml, false, "Log the XML machine configuration");
DEFINE_int32(memory_mb, 2048,
             "Total amount of memory available for guest, MB.");
std::string g_default_mempath{GetPerInstanceDefault("/var/run/shm/cvd-")};
DEFINE_string(mempath, g_default_mempath.c_str(),
              "Target location for the shmem file.");
std::string g_default_mobile_interface{GetPerInstanceDefault("cvd-mobile-")};
DEFINE_string(mobile_interface, g_default_mobile_interface.c_str(),
              "Network interface to use for mobile networking");
std::string g_default_qemusocket = GetDefaultPerInstancePath("ivshmem_socket_qemu");
DEFINE_string(qemusocket, g_default_qemusocket.c_str(), "QEmu socket path");
std::string g_default_serial_number{GetPerInstanceDefault("CUTTLEFISHCVD")};
DEFINE_string(serial_number, g_default_serial_number.c_str(),
              "Serial number to use for the device");
DEFINE_string(system_image_dir,
              StringFromEnv("ANDROID_PRODUCT_OUT", StringFromEnv("HOME", ".")),
              "Location of the system partition images.");
DEFINE_string(vendor_image, "", "Location of the vendor partition image.");

std::string g_default_uuid{
    GetPerInstanceDefault("699acfc4-c8c4-11e7-882b-5065f31dc1")};
DEFINE_string(uuid, g_default_uuid.c_str(),
              "UUID to use for the device. Random if not specified");
DEFINE_bool(deprecated_boot_completed, false, "Log boot completed message to"
            " host kernel. This is only used during transition of our clients."
            " Will be deprecated soon.");

namespace {
const std::string kDataPolicyUseExisting = "use_existing";
const std::string kDataPolicyCreateIfMissing = "create_if_missing";
const std::string kDataPolicyAlwaysCreate = "always_create";

Json::Value LoadLayoutFile(const std::string& file) {
  char real_file_path[PATH_MAX];
  if (realpath(file.c_str(), real_file_path) == nullptr) {
    LOG(FATAL) << "Could not get real path for file " << file << ": "
               << strerror(errno);
  }

  Json::Value result;
  Json::Reader reader;
  std::ifstream ifs(real_file_path);
  if (!reader.parse(ifs, result)) {
    LOG(FATAL) << "Could not read layout file " << file << ": "
               << reader.getFormattedErrorMessages();
  }
  return result;
}

// VirtualUSBManager manages virtual USB device presence for Cuttlefish.
class VirtualUSBManager {
 public:
  VirtualUSBManager(const std::string& usbsocket,
                    const std::string& android_usbipsocket)
      : adb_{usbsocket, android_usbipsocket},
        usbip_{android_usbipsocket, adb_.Pool()} {}

  ~VirtualUSBManager() = default;

  // Initialize Virtual USB and start USB management thread.
  void Start() {
    CHECK(adb_.Init()) << "Could not initialize Virtual ADB server";
    CHECK(usbip_.Init()) << "Could not start USB/IP server";
    thread_.reset(new std::thread([this]() { Thread(); }));
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
  std::unique_ptr<std::thread> thread_;

  VirtualUSBManager(const VirtualUSBManager&) = delete;
  VirtualUSBManager& operator=(const VirtualUSBManager&) = delete;
};

// IVServerManager takes care of serving shared memory segments between
// Cuttlefish and host-side daemons.
class IVServerManager {
 public:
  IVServerManager(const Json::Value& json_root)
      : server_(ivserver::IVServerOptions(FLAGS_layout, FLAGS_mempath,
                                          FLAGS_qemusocket,
                                          vsoc::GetDomain()),
                json_root) {}

  ~IVServerManager() = default;

  // Start IVServer thread.
  void Start() {
    thread_.reset(new std::thread([this]() { server_.Serve(); }));
  }

 private:
  ivserver::IVServer server_;
  std::unique_ptr<std::thread> thread_;

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
    thread_.reset(new std::thread([this]() { Thread(); }));
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
  std::unique_ptr<std::thread> thread_;

  KernelLogMonitor(const KernelLogMonitor&) = delete;
  KernelLogMonitor& operator=(const KernelLogMonitor&) = delete;
};

void subprocess(const char* const* command) {
  pid_t pid = fork();
  if (!pid) {
    int rval = execv(command[0], const_cast<char* const*>(command));
    // No need for an if: if exec worked it wouldn't have returned
    LOG(ERROR) << "exec of " << command[0] << " failed (" << strerror(errno)
               << ")";
    exit(rval);
  }
  if (pid == -1) {
    LOG(ERROR) << "fork of " << command[0] << " failed (" << strerror(errno)
               << ")";
  } else {
    waitpid(pid, 0, 0);
  }
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
  subprocess(dd_command);
  const char* mkfs_command[]{
    "/usr/bin/sudo", "/sbin/mkfs", "-t", image_fmt.c_str(), image.c_str(), NULL};
  subprocess(mkfs_command);
}

void RemoveFile(const std::string& file) {
  LOG(INFO) << "Removing " << file;
  const char* rm_command[]{
    "/usr/bin/sudo", "/bin/rm", "-f", file.c_str(), NULL};
  subprocess(rm_command);
}
}  // anonymous namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  LOG_IF(FATAL, FLAGS_system_image_dir.empty())
      << "--system_image_dir must be specified.";

  std::string per_instance_dir = vsoc::GetDefaultPerInstanceDir();
  struct stat unused;
  if ((stat(per_instance_dir.c_str(), &unused) == -1) && (errno == ENOENT)) {
    LOG(INFO) << "Setting up " << per_instance_dir;
    const char* mkdir_command[]{
        "/usr/bin/sudo",          "/bin/mkdir", "-m", "0775",
        per_instance_dir.c_str(), NULL};
    subprocess(mkdir_command);
    std::string owner_group{getenv("USER")};
    owner_group += ":libvirt-qemu";
    const char* chown_command[]{"/usr/bin/sudo", "/bin/chown",
                                owner_group.c_str(), per_instance_dir.c_str(),
                                NULL};
    subprocess(chown_command);
  }

  // If user did not specify location of either of these files, expect them to
  // be placed in --system_image_dir location.
  if (FLAGS_kernel.empty()) {
    FLAGS_kernel = FLAGS_system_image_dir + "/kernel";
  }

  if (FLAGS_kernel_command_line.empty()) {
    FLAGS_kernel_command_line = FLAGS_system_image_dir + "/cmdline";
  }

  if (FLAGS_initrd.empty()) {
    FLAGS_initrd = FLAGS_system_image_dir + "/ramdisk.img";
  }

  if (FLAGS_cache_image.empty()) {
    FLAGS_cache_image = FLAGS_system_image_dir + "/cache.img";
  }

  if (FLAGS_data_policy == kDataPolicyCreateIfMissing) {
    FLAGS_data_image = FLAGS_system_image_dir + "/userdata_blank.img";
    if (FLAGS_blank_data_image_mb <= 0) {
      LOG(FATAL) << "You should use -blank_data_image_mb with -data_policy="
                 << kDataPolicyCreateIfMissing;
    }
    // Create a blank data image if the image doesn't exist yet
    if ((stat(FLAGS_data_image.c_str(), &unused) == -1) && (errno == ENOENT)) {
      CreateBlankImage(
          FLAGS_data_image, FLAGS_blank_data_image_mb, FLAGS_blank_data_image_fmt);
    } else {
      LOG(INFO) << FLAGS_data_image << " exists. Not creating it.";
    }
  } else if (FLAGS_data_policy == kDataPolicyAlwaysCreate) {
    FLAGS_data_image = FLAGS_system_image_dir + "/userdata_blank.img";
    if (FLAGS_blank_data_image_mb <= 0) {
      LOG(FATAL) << "You should use -blank_data_image_mb with -data_policy="
                 << kDataPolicyAlwaysCreate;
    }
    RemoveFile(FLAGS_data_image);
    CreateBlankImage(
        FLAGS_data_image, FLAGS_blank_data_image_mb, FLAGS_blank_data_image_fmt);
  } else if (FLAGS_data_policy == kDataPolicyUseExisting) {
    // Do nothing. Use FLAGS_data_image.
    if (FLAGS_blank_data_image_mb > 0) {
      LOG(FATAL) << "You should NOT use -blank_data_image_mb with -data_policy="
                 << kDataPolicyUseExisting;
    }
  } else {
    LOG(FATAL) << "Invalid data_policy: " << FLAGS_data_policy;
  }

  if (FLAGS_data_image.empty()) {
    FLAGS_data_image = FLAGS_system_image_dir + "/userdata.img";
  }

  if (FLAGS_vendor_image.empty()) {
    FLAGS_vendor_image = FLAGS_system_image_dir + "/vendor.img";
  }

  Json::Value json_root = LoadLayoutFile(FLAGS_layout);

  // Each of these calls is free to fail and terminate launch if file does not
  // exist or could not be created.
  auto system_partition = config::FilePartition::ReuseExistingFile(
      FLAGS_system_image_dir + "/system.img");
  auto data_partition =
      config::FilePartition::ReuseExistingFile(FLAGS_data_image);
  auto cache_partition =
      config::FilePartition::ReuseExistingFile(FLAGS_cache_image);
  auto vendor_partition =
      config::FilePartition::ReuseExistingFile(FLAGS_vendor_image);

  std::ostringstream cmdline;
  std::ifstream t(FLAGS_kernel_command_line);
  if (!t) {
    LOG(FATAL) << "Unable to open " << FLAGS_kernel_command_line;
  }
  cmdline << t.rdbuf();
  t.close();
  cmdline << " androidboot.serialno=" << FLAGS_serial_number;
  if (FLAGS_extra_kernel_command_line.size()) {
    cmdline << " " << FLAGS_extra_kernel_command_line;
  }

  std::string entropy_source = "/dev/urandom";

  config::GuestConfig cfg;
  cfg.SetID(FLAGS_instance)
      .SetVCPUs(FLAGS_cpus)
      .SetMemoryMB(FLAGS_memory_mb)
      .SetKernelName(FLAGS_kernel)
      .SetInitRDName(FLAGS_initrd)
      .SetKernelArgs(cmdline.str())
      .SetIVShMemSocketPath(FLAGS_qemusocket)
      .SetIVShMemVectorCount(json_root["vsoc_device_regions"].size())
      .SetSystemPartitionPath(system_partition->GetName())
      .SetCachePartitionPath(cache_partition->GetName())
      .SetDataPartitionPath(data_partition->GetName())
      .SetVendorPartitionPath(vendor_partition->GetName())
      .SetMobileBridgeName(FLAGS_mobile_interface)
      .SetEntropySource(entropy_source)
      .SetDisableDACSecurity(FLAGS_disable_dac_security)
      .SetDisableAppArmorSecurity(FLAGS_disable_app_armor_security)
      .SetUUID(FLAGS_uuid);
  cfg.SetUSBV1SocketName(
      GetDefaultPerInstancePath(cfg.GetInstanceName() + "-usb"));
  cfg.SetKernelLogSocketName(
      GetDefaultPerInstancePath(cfg.GetInstanceName() + "-kernel-log"));

  std::string xml = cfg.Build();
  if (FLAGS_log_xml) {
    LOG(INFO) << "Using XML:\n" << xml;
  }

  VirtualUSBManager vadb(cfg.GetUSBV1SocketName(),
                         GetPerInstanceDefault("android_usbip"));
  vadb.Start();
  IVServerManager ivshmem(json_root);
  ivshmem.Start();
  KernelLogMonitor kmon(cfg.GetKernelLogSocketName(),
                        GetDefaultPerInstancePath("kernel.log"),
                        FLAGS_deprecated_boot_completed);
  kmon.Start();

  sleep(1);

  // Initialize the regions that require it before the VM starts.
  PreLaunchInitializers::Initialize();

  FILE* launch = popen(FLAGS_launch_command.c_str(), "w");
  if (!launch) {
    LOG(FATAL) << "Unable to execute " << FLAGS_launch_command;
  }
  int rval = fputs(xml.c_str(), launch);
  if (rval == EOF) {
    LOG(FATAL) << "Launch command exited while accepting XML";
  }
  int exit_code = pclose(launch);
  if (exit_code) {
    LOG(FATAL) << "Launch command exited with status " << exit_code;
  }
  pause();
}
