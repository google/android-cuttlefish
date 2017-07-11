/*
 * Copyright (C) 2016 The Android Open Source Project
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
#define GCE_INIT_DEBUG 0
#define LOWER_SYSTEM_MOUNT_POINT "/var/system_lower"
#define UPPER_SYSTEM_MOUNT_POINT "/var/system_upper"

#include <map>
#include <memory>
#include <sstream>
#include <string>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include <glog/logging.h>

#include "common/libs/fs/gce_fs.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/metadata/display_properties.h"
#include "common/libs/metadata/gce_metadata_attributes.h"
#include "common/libs/metadata/get_partition_num.h"
#include "common/libs/metadata/initial_metadata_reader.h"
#include "common/libs/metadata/metadata_query.h"

#include "guest/gce_init/environment_setup.h"
#include "guest/gce_init/properties.h"
#include "guest/gce_network/namespace_aware_executor.h"
#include "guest/gce_network/netlink_client.h"
#include "guest/gce_network/network_interface_manager.h"
#include "guest/gce_network/network_namespace_manager.h"
#include "guest/gce_network/sys_client.h"
#include "guest/ramdisk/unpack_ramdisk.h"

#if defined(__LP64__)
#define LIBRARY_PATH_SYSTEM "/system/lib64/"
#define LIBRARY_PATH_HARDWARE "/system/lib64/hw/"
#define LIBRARY_PATH_VENDOR "/vendor/lib64/hw/"
#define TARGET_LIB_PATH_RIL "/target/system/lib64/libvsoc-ril%s.so"
#define TARGET_LIB_PATH_HW_COMPOSER \
  "/target/system/lib64/hw/hwcomposer.vsoc%s.so"
#else
#define LIBRARY_PATH_SYSTEM "/system/lib/"
#define LIBRARY_PATH_HARDWARE "/system/lib/hw/"
#define LIBRARY_PATH_VENDOR "/vendor/lib/hw/"
#define TARGET_LIB_PATH_RIL "/target/system/lib/libvsoc-ril%s.so"
#define TARGET_LIB_PATH_HW_COMPOSER "/target/system/lib/hw/hwcomposer.vsoc%s.so"
#endif

#define OUTER_INTERFACE_CONFIG_DIR "/var/run"

using avd::EnvironmentSetup;
using avd::InitialMetadataReader;
using avd::NamespaceAwareExecutor;
using avd::NetlinkClient;
using avd::NetworkInterfaceManager;
using avd::NetworkNamespaceManager;
using avd::SysClient;
using avd::kCloneNewNet;

namespace {
// Linux device major and minor numbers can be found here:
// http://lxr.free-electrons.com/source/Documentation/devices.txt
struct DeviceSpec {
  int major;
  int minor;
  int mode;
  const char* path;
};

const DeviceSpec simple_char_devices[] = {{1, 3, 0666, "/dev/null"},
                                          {1, 8, 0666, "/dev/random"},
                                          {1, 9, 0666, "/dev/urandom"},
                                          {1, 11, 0644, "/dev/kmsg"},
                                          {10, 237, 0600, "/dev/loop-control"}};

const char kDevBlockDir[] = "/dev/block";
const char kCustomInitFileName[] = "/target/init.metadata.rc";
const char kMetadataPropertiesFileName[] = "/target/metadata_properties.rc";
const char kEmergencyShell[] = "/system/bin/sh";
const char kMultibootDevice[] = "/dev/block/sda";
const int kMultibootPartition = 1;
const char kDefaultPartitionsPath[] = "/target/partitions";

const char kCuttlefishParameter[] = "CUTTLEFISH";

#define AVD_WARN "AVD WARNING"

// Place all files and folders you need bind-mounted here.
// All files and folders are required to be part of the (target) dir structure.
// Examples:
// - { "/fstab.gce_x86.template", "/fstab" }
//   bind-mounts file /fstab.gce_x86.template to /fstab.
//   The /fstab file will be created and, for debugging purposes, will be
//   initialized with the path of the source file.
// - { "/system", "/system.2" }
//   bind-mounts directory /system to /system.2.
//   The /system.2 directory will be created.
const char* kBindFiles[][2] = {{NULL, NULL}};

}  // namespace

class Container {
 public:
  enum DeviceType {
    kDeviceTypeUnknown,
    kDeviceTypeWifi,
    kDeviceType3G,
  };

  Container() : device_type_(kDeviceTypeUnknown), is_cuttlefish_(false) {}

  ~Container() {}

  // Initializes minimum environment needed to launch basic commands.
  // This section should eventually be deleted as we progress with containers.
  bool InitializeMinEnvironment(std::stringstream* error);

  // Managers require a minimum working environment to be created.
  const char* CreateManagers();

  const char* InitializeNamespaces();
  const char* ConfigureNetworkCommon();
  const char* ConfigureNetworkMobile();

  const char* FetchMetadata();
  const char* InitTargetFilesystem();
  const char* BindFiles();
  const char* ApplyCustomization();
  const char* PivotToNamespace(const char* name);

  const char* CleanUp();

 private:
  const char* ApplyCustomInit();
  const char* ApplyMetadataProperties();
  const char* Bind(const char* source, const char* target);
  const char* SelectVersion(const char* name, const char* version,
                            const char* name_pattern);

  std::unique_ptr<SysClient> sys_client_;
  std::unique_ptr<NetlinkClient> nl_client_;
  std::unique_ptr<NetworkNamespaceManager> ns_manager_;
  std::unique_ptr<NetworkInterfaceManager> if_manager_;
  std::unique_ptr<NamespaceAwareExecutor> executor_;
  std::unique_ptr<EnvironmentSetup> setup_;
  InitialMetadataReader* reader_;
  std::string android_version_;
  DeviceType device_type_;
  bool is_cuttlefish_;

  Container(const Container&);
  Container& operator=(const Container&);
};

static const char* MountTmpFs(const char* mount_point, const char* size) {
  if (gce_fs_prepare_dir(mount_point, 0700, 0, 0) == 0) {
    if (mount("tmpfs", mount_point, "tmpfs", MS_NOSUID, size) == 0) {
      return NULL;
    } else {
      LOG(ERROR) << "Could not mount tmpfs at " << mount_point << ": "
                 << strerror(errno);
    }
  } else {
    LOG(ERROR) << "Could not prepare dir " << mount_point << ": "
               << strerror(errno);
  }

  return "tmpfs mount failed.";
}

bool CreateDeviceNode(const char* name, int flags, int major, int minor) {
  dev_t dev = makedev(major, minor);
  mode_t old_mask = umask(0);
  int rval = TEMP_FAILURE_RETRY(mknod(name, flags, dev));
  umask(old_mask);
  if (rval == -1) {
    LOG(ERROR) << "mknod failed for " << name << ": " << strerror(errno);
    return false;
  }
  return true;
}

bool CreateBlockDeviceNodes() {
  AutoCloseFILE f(fopen("/proc/partitions", "r"));
  char line[160];
  char device[160];
  if (!f) {
    LOG(ERROR) << "open of /proc/partitions failed: " << strerror(errno);
    return false;
  }

  if (gce_fs_prepare_dir(kDevBlockDir, 0700, 0, 0) == -1) {
    LOG(INFO) << "gs_fs_prepare_dir(" << kDevBlockDir
              << ") failed: " << strerror(errno);
    return false;
  }

  int major, minor;
  long long blocks;
  bool found = false;
  while (!found && fgets(line, sizeof(line), f)) {
    int fields = sscanf(line, "%d%d%lld%s", &major, &minor, &blocks, device);
    if (fields == 4) {
      AutoFreeBuffer dev_path;
      dev_path.PrintF("%s/%s", kDevBlockDir, device);
      if (!CreateDeviceNode(dev_path.data(), S_IFBLK | S_IRUSR | S_IWUSR, major,
                            minor)) {
        return false;
      }
    }
  }
  return true;
}

// Mounts a filesystem.
// Returns true if the mount happened.
bool MountFilesystem(const char* fs, const char* disk, long partition_num,
                     const char* dir,
                     unsigned long mount_flags = MS_RDONLY | MS_NODEV) {
  AutoFreeBuffer temp_dev;
  if (gce_fs_prepare_dir(dir, 0700, 0, 0) == -1) {
    LOG(ERROR) << "gs_fs_prepare_dir(" << dir
               << ") failed: " << strerror(errno);
    return false;
  }
  if (disk && *disk) {
    if (partition_num) {
      temp_dev.PrintF("%s%ld", disk, partition_num);
    } else {
      temp_dev.SetToString(disk);
    }
  }
  if (TEMP_FAILURE_RETRY(mount(temp_dev.data(), dir, fs, mount_flags, NULL)) ==
      -1) {
    LOG(ERROR) << "mount of " << dir << " failed: " << strerror(errno);
    return false;
  }
  return true;
}

// Copies a file.
// Returns true if the copy succeeded.
static bool CopyFile(const char* in_path, const char* out_path) {
  AutoCloseFILE in(fopen(in_path, "rb"));
  if (in.IsError()) {
    LOG(ERROR) << "unable to open input file " << in_path << ": "
               << strerror(errno);
    return false;
  }
  AutoCloseFILE out(fopen(out_path, "wb"));
  if (out.IsError()) {
    LOG(ERROR) << "unable to open output file " << out_path << ": "
               << strerror(errno);
    return false;
  }
  if (!out.CopyFrom(in)) {
    return false;
  }
  return true;
}

bool IsCuttlefish() {
  AutoFreeBuffer cmdline;
  avd::SharedFD cmdlinefd = avd::SharedFD::Open("/proc/cmdline", O_RDONLY, 0);

  // One doesn't simply stat a /proc file.
  // On more serious note: yes, I'm making an assumption here regarding length
  // of kernel command line - that it's no longer than 16k.
  // Linux allows command line length to be up to MAX_ARG_STRLEN long, which
  // is essentially 32 * PAGE_SIZE (~256K). I don't think we'll get that far any
  // time soon.
  if (!cmdlinefd->IsOpen()) {
    LOG(WARNING) << "Unable to read /proc/cmdline: " << cmdlinefd->StrError();
    return false;
  }

  // 16k + 1 padding zero.
  cmdline.Resize(16384 + 1);
  cmdlinefd->Read(cmdline.data(), cmdline.size());
  LOG(WARNING) << cmdline.data();
  return (strstr(cmdline.data(), kCuttlefishParameter) != NULL);
}

static bool MountSystemPartition(const char* partitions_path,
                                 const char* mount_point, bool is_cuttlefish) {
  mode_t save = umask(0);
  int result = TEMP_FAILURE_RETRY(mkdir(mount_point, 0777));
  umask(save);
  if ((result == -1) && (errno != EEXIST)) {
    LOG(ERROR) << "skipping " << mount_point
               << ": mkdir failed: " << strerror(errno);
    return false;
  }

  // Fixed fallback values, used with cuttlefish.
  const char* boot_device = "/dev/block/vdb";
  long system_partition_num = 0;

  if (!is_cuttlefish) {
    boot_device = kMultibootDevice;
    system_partition_num = GetPartitionNum("system", partitions_path);
    if (system_partition_num == -1) {
      LOG(ERROR) << "unable to find system partition";
      return false;
    }
  }

  if (!MountFilesystem("ext4", boot_device, system_partition_num,
                       mount_point)) {
    LOG(ERROR) << "unable to mount system partition " << boot_device
               << system_partition_num;
    return false;
  }

  return true;
}

// Attempt to mount a system overlay, returning true only if the overlay
// was created.
// The overlay will be mounted read-only here to avoid a serious security
// issues: mounting the upper filesystem read-write would allow attackers
// to modify the "read-only" overlay via the upper mount point.
//
// This means that we need to pass additional data to adb to allow remount to
// work as expected.
// We use the unused device parameter to pass a hint to adb to coordinate the
// remount. In addition, we create a directory to allow adb to construct a
// writable overlay that will be bound to /system.
static bool MountSystemOverlay(const InitialMetadataReader& reader,
                               bool is_cuttlefish) {
  const char* system_overlay_device =
      reader.GetValueForKey(GceMetadataAttributes::kSystemOverlayDeviceKey);
  if (!system_overlay_device) {
    LOG(INFO) << "No system overlay device.";
    return false;
  }
  if (!MountFilesystem("ext4", system_overlay_device, 0,
                       UPPER_SYSTEM_MOUNT_POINT)) {
    LOG(INFO) << "Could not mount overlay device " << system_overlay_device;
    return false;
  }
  if (!MountSystemPartition(kDefaultPartitionsPath, LOWER_SYSTEM_MOUNT_POINT,
                            is_cuttlefish)) {
    LOG(INFO) << "Could not mount " << kMultibootDevice << " from "
              << kDefaultPartitionsPath << " at " << LOWER_SYSTEM_MOUNT_POINT;
    return false;
  }
  gce_fs_prepare_dir("/target/system", 0700, 0, 0);
  const char* const remount_hint = "uppermntpt=" UPPER_SYSTEM_MOUNT_POINT
                                   ","
                                   "upperdir=" UPPER_SYSTEM_MOUNT_POINT
                                   "/data,"
                                   "workdir=" UPPER_SYSTEM_MOUNT_POINT
                                   "/work,"
                                   "lowerdir=" LOWER_SYSTEM_MOUNT_POINT;
  if (mount(remount_hint, "/target/system", "overlay", MS_RDONLY | MS_NODEV,
            "lowerdir=" UPPER_SYSTEM_MOUNT_POINT
            "/data:" LOWER_SYSTEM_MOUNT_POINT) == -1) {
    LOG(ERROR) << "Overlay mount failed, falling back to base system: "
               << strerror(errno);
    return false;
  }
  if (gce_fs_prepare_dir("/target/system_rw", 0700, 0, 0) == -1) {
    LOG(ERROR) << "Failed to create /system_rw. adb remount will fail";
  }
  return true;
}

class BootPartitionMounter {
 public:
  BootPartitionMounter(bool is_cuttlefish)
      : is_cuttlefish_(is_cuttlefish), is_mounted_(false) {
    if (!is_cuttlefish_) {
      // All mounts of disk partitions must be read-only.
      is_mounted_ = MountFilesystem("ext4", kMultibootDevice,
                                    kMultibootPartition, multiboot_location_);
    }
  }

  ~BootPartitionMounter() {
    if (!is_cuttlefish_ && is_mounted_) {
      umount2(multiboot_location_, MNT_FORCE);
    }
  }

  bool IsSuccess() const { return is_mounted_ || is_cuttlefish_; }

 private:
  bool is_cuttlefish_;
  bool is_mounted_;
  const char* const multiboot_location_ = "/boot";
};

bool Init(Container* container, std::stringstream* error) {
  if (!container->InitializeMinEnvironment(error)) {
    return false;
  }

  const char* res = container->CreateManagers();
  if (res) {
    *error << res;
    return false;
  }

  res = container->InitializeNamespaces();
  if (res) {
    *error << res;
    return false;
  }

  res = container->PivotToNamespace(NetworkNamespaceManager::kOuterNs);
  if (res) {
    *error << res;
    return false;
  }

  res = container->ConfigureNetworkCommon();
  if (res) {
    *error << res;
    return false;
  }

  res = container->FetchMetadata();
  if (res) {
    *error << res;
    return false;
  }

  res = container->PivotToNamespace(NetworkNamespaceManager::kAndroidNs);
  if (res) {
    *error << res;
    return false;
  }

  res = container->InitTargetFilesystem();
  if (res) {
    *error << res;
    return false;
  }

  res = container->ApplyCustomization();
  if (res) {
    *error << res;
    return false;
  }

  LOG(INFO) << "Pivoting to Android Init";

  res = container->CleanUp();
  if (res) {
    *error << res;
    return false;
  }

  // Chain to the Android init process.
  int rval = TEMP_FAILURE_RETRY(execl("/init", "/init", NULL));
  if (rval == -1) {
    LOG(ERROR) << "execl failed: " << strerror(errno);
    *error << "Could not exec init.";
    return false;
  }

  *error << "exec finished unexpectedly.";
  return false;
}

bool Container::InitializeMinEnvironment(std::stringstream* error) {
  // Set up some initial enviromnent stuff that we need for reliable shared
  // libraries.
  if (!MountFilesystem("proc", NULL, 0, "/proc", 0)) {
    *error << "Could not mount initial /proc.";
    return false;
  }

  if (!MountFilesystem("sysfs", NULL, 0, "/sys", 0)) {
    *error << "Could not mount initial /sys.";
    return false;
  }

  // Set up tmpfs partitions for /dev. Normally Android's init would handle
  // this. However, processes are going to retain opens to /dev/__properties__,
  // and if it's on the root filesystem we won't be able to remount it
  // read-only later in the boot.
  //
  // We need to initialize the properties because K's bionic will
  // crash if they're not in place.
  //
  // This works because init's attempt to mount a tmpfs later will silently
  // overlay the existing mount.
  const char* res;
  res = MountTmpFs("/dev", "mode=0755");
  if (res) {
    *error << res;
    return false;
  }

  // Set up tmpfs partitions for /var
  res = MountTmpFs("/var", "mode=0755");
  if (res) {
    *error << res;
    return false;
  }

  for (size_t i = 0;
       i < sizeof(simple_char_devices) / sizeof(simple_char_devices[0]); ++i) {
    if (!CreateDeviceNode(
            simple_char_devices[i].path, S_IFCHR | simple_char_devices[i].mode,
            simple_char_devices[i].major, simple_char_devices[i].minor)) {
      *error << "Could not create " << simple_char_devices[i].path;
      return false;
    }
  }

  if (!CreateBlockDeviceNodes()) {
    *error << "Could not create block device nodes.";
    return false;
  }

  is_cuttlefish_ = IsCuttlefish();

  // Mount the boot partition so we can get access the configuration and
  // ramdisks there.
  // Unmount this when we're done so that this doesn't interefere with mount
  // namespaces later.
  {
    BootPartitionMounter boot_mounter(is_cuttlefish_);

    if (!boot_mounter.IsSuccess()) {
      *error << "Could not mount multiboot /boot partition.";
      return false;
    }

    // Mount the default system partition so we can issue a DHCP request.
    if (!MountSystemPartition("/boot/targets/default/partitions", "/system",
                              is_cuttlefish_)) {
      *error << "Could not mount multiboot /system partition.";
      return false;
    }
  }

  if (setenv("LD_LIBRARY_PATH",
             LIBRARY_PATH_SYSTEM ":" LIBRARY_PATH_HARDWARE
                                 ":" LIBRARY_PATH_VENDOR,
             1) == -1) {
    *error << "Failed to set LD_LIBRARY_PATH.";
    return false;
  }

  if (gce_fs_mkdirs("/data", 0755) != 0) {
    *error << "Could not create /data folder.";
    return false;
  }

  return true;
}

const char* Container::CreateManagers() {
  sys_client_.reset(SysClient::New());
  if (!sys_client_.get()) return "Unable to create sys client.";

  nl_client_.reset(NetlinkClient::New(sys_client_.get()));
  if (!nl_client_.get()) return "Unable to create netlink client.";

  ns_manager_.reset(NetworkNamespaceManager::New(sys_client_.get()));
  if (!ns_manager_.get()) return "Unable to create namespace manager.";

  if_manager_.reset(
      NetworkInterfaceManager::New(nl_client_.get(), ns_manager_.get()));
  if (!if_manager_.get()) return "Unable to create interface manager.";

  executor_.reset(
      NamespaceAwareExecutor::New(ns_manager_.get(), sys_client_.get()));
  if (!executor_.get()) return "Unable to create executor.";

  setup_.reset(new EnvironmentSetup(executor_.get(), ns_manager_.get(),
                                    if_manager_.get(), sys_client_.get()));

  return NULL;
}

const char* Container::InitializeNamespaces() {
  if (!setup_->CreateNamespaces()) return "Could not create namespaces.";

  return NULL;
}

const char* Container::ConfigureNetworkCommon() {
  // Allow the scripts started by DHCP to update the MTU.
  if (gce_fs_mkdirs(OUTER_INTERFACE_CONFIG_DIR,
                    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
    LOG(ERROR) << "Unable to create " << OUTER_INTERFACE_CONFIG_DIR << ": "
               << strerror(errno);
    return "Could not create host interface env folder.";
  }

  if (!setup_->ConfigureNetworkCommon()) {
    return "Failed to configure common network.";
  }

  if (is_cuttlefish_) {
    if (!setup_->ConfigurePortForwarding())
      return "Failed to configure port forwarding.";
  }

  return NULL;
}

const char* Container::ConfigureNetworkMobile() {
  LOG(INFO) << "Configuring mobile network";
  if (!setup_->ConfigureNetworkMobile())
    return "Failed to configure mobile network.";

  return NULL;
}

const char* Container::FetchMetadata() {
  // Now grab the metadata that will tell us which image we need to boot.
  AutoFreeBuffer buffer;

  // Metadata server is offering metadata only within Android namespace.
  // Flip current namespace temporarily to construct the reader.
  if (sys_client_->SetNs(ns_manager_->GetNamespaceDescriptor(
                             NetworkNamespaceManager::kAndroidNs),
                         kCloneNewNet) < 0) {
    LOG(ERROR) << "Failed to switch namespace: " << strerror(errno);
    return "Could not switch namespace to initiate metadata connection.";
  }

  // Wait for initial metadata.
  // It may not be instantly available; keep looping until it pops up.
  MetadataQuery* query = MetadataQuery::New();
  LOG(INFO) << "Waiting for initial metadata...";
  while (!query->QueryServer(&buffer)) {
    usleep(100 * 1000);
  }
  LOG(INFO) << "Metadata ready.";
  delete query;

  reader_ = InitialMetadataReader::getInstance();
  // Return to original network namespace.
  if (sys_client_->SetNs(ns_manager_->GetNamespaceDescriptor(
                             NetworkNamespaceManager::kOuterNs),
                         kCloneNewNet) < 0) {
    LOG(ERROR) << "Failed to switch namespace: " << strerror(errno);
    return "Could not switch namespace after initiating metadata connection.";
  }

  const char* version =
      reader_->GetValueForKey(GceMetadataAttributes::kAndroidVersionKey);
  if (version) {
    android_version_ = version;
  } else {
    android_version_ = "default";
  }

  LOG(INFO) << "Booting android_version=" << android_version_.c_str();

  return NULL;
}

const char* Container::InitTargetFilesystem() {
  // We need to have our /target folder mounted in order to re-mount root.
  // To achieve this, we keep two folders:
  // - /target_mount, a directory, where everything is kept,
  // - /target, bind-mounted to /target_mount.
  if (gce_fs_mkdirs("/target_mount", 0755) != 0)
    return "Could not create /target_mount folder.";
  if (gce_fs_mkdirs("/target", 0755) != 0)
    return "Could not create /target folder.";
  if (mount("/target_mount", "/target", NULL, MS_BIND, NULL))
    return "Could not mount /target_mount.";

  if (gce_fs_mkdirs("/target/boot", 0755) != 0)
    return "Could not create /target/boot folder.";
  if (gce_fs_mkdirs("/target/system", 0755) != 0)
    return "Could not create /target/system folder.";
  if (gce_fs_mkdirs("/target/proc", 0755) != 0)
    return "Could not create /target/proc folder.";
  if (gce_fs_mkdirs("/target/sys", 0755) != 0)
    return "Could not create /target/sys folder.";
  if (gce_fs_mkdirs("/target/var", 0755) != 0)
    return "Could not create /target/var folder.";
  if (gce_fs_mkdirs("/target" EPHEMERAL_FS_BLOCK_DIR, 0755) != 0)
    return "Could not create /target" EPHEMERAL_FS_BLOCK_DIR " folder.";

  // Set up tmpfs for the ephemeral block devices
  // This leaves 512MB of RAM for the system on a n1-standard-1 (our smallest
  // supported configuration). 512MB is the minimum supported by
  // KitKat. The Nexus S shipped in this configuration.
  const char* res;
  res = MountTmpFs("/target" EPHEMERAL_FS_BLOCK_DIR, "size=86%");
  if (res) return res;

  {
    BootPartitionMounter boot_mounter(is_cuttlefish_);

    if (!boot_mounter.IsSuccess()) {
      return "Could not mount multiboot /boot partition.";
    }

    if (!is_cuttlefish_) {
      // Unpack the RAM disk here because gce_mount_hander needs the fstab
      // template
      AutoFreeBuffer ramdisk_path;
      ramdisk_path.PrintF("/boot/targets/%s/ramdisk", android_version_.c_str());
      UnpackRamdisk(ramdisk_path.data(), "/target");

      // Place the partitions file in root. It will be needed by
      // gce_mount_handler
      AutoFreeBuffer partitions_path;
      partitions_path.PrintF("/boot/targets/%s/partitions",
                             android_version_.c_str());
      CopyFile(partitions_path.data(), kDefaultPartitionsPath);
    } else {
      UnpackRamdisk("/dev/block/vda", "/target");
    }
  }

  if (!MountSystemOverlay(*reader_, is_cuttlefish_) &&
      !MountSystemPartition(kDefaultPartitionsPath, "/target/system",
                            is_cuttlefish_))
    return "Unable to mount /target/system.";

  return NULL;
}

const char* Container::BindFiles() {
  for (int index = 0; kBindFiles[index][0] != NULL; ++index) {
    AutoFreeBuffer source, target;

    source.PrintF("/target%s", kBindFiles[index][0]);
    target.PrintF("/target%s", kBindFiles[index][1]);

    const char* res = Bind(source.data(), target.data());
    if (res) return res;
  }

  const char* res = NULL;

  res = SelectVersion("RIL", GceMetadataAttributes::kRilVersionKey,
                      TARGET_LIB_PATH_RIL);
  if (res) return res;

  res =
      SelectVersion("HWComposer", GceMetadataAttributes::kHWComposerVersionKey,
                    TARGET_LIB_PATH_HW_COMPOSER);
  if (res) return res;

  res = SelectVersion("VNC", GceMetadataAttributes::kVncServerVersionKey,
                      "/target/system/bin/vnc_server%s");
  if (res) {
    return res;
  }

  return NULL;
}

const char* Container::SelectVersion(const char* name, const char* metadata_key,
                                     const char* pattern) {
  const char* version = reader_->GetValueForKey(metadata_key);
  if (!version) return NULL;

  char default_version[PATH_MAX];
  char selected_version[PATH_MAX];

  snprintf(&default_version[0], sizeof(default_version), pattern, "");

  struct stat sb;
  if (stat(default_version, &sb) < 0) {
    LOG(WARNING) << "Ignoring " << name << " variant setting " << version
                 << ": not applicable.";
    return NULL;
  }

  if (!version || !strcmp(version, "DEFAULT")) {
    // No change.
    return NULL;
  } else if (!strcmp(version, "TESTING")) {
    snprintf(&selected_version[0], sizeof(selected_version), pattern,
             "-testing");
  } else if (!strcmp(version, "DEPRECATED")) {
    snprintf(&selected_version[0], sizeof(selected_version), pattern,
             "-deprecated");
  } else {
    LOG(WARNING) << "Variant " << version << " not valid for " << name
                 << ". Using default.";
    return NULL;
  }

  // So, user specified a different variant of module, but this variant is
  // not explicitly specified.
  if (stat(selected_version, &sb) < 0) {
    LOG(WARNING) << "Ignoring " << name << " variant setting " << version
                 << ": not available.";
    return NULL;
  }

  LOG(WARNING) << "Switching " << name << " to " << version << " variant";
  return Bind(selected_version, default_version);
}

const char* Container::Bind(const char* source, const char* target) {
  struct stat sb, tb;
  if (stat(source, &sb) < 0) {
    LOG(ERROR) << "Could not stat bind file " << source << ": "
               << strerror(errno);
    return "Could not find bind source.";
  }

  if (stat(target, &tb) < 0) {
    LOG(ERROR) << "Could not bind-mount to target " << target << ": "
               << strerror(errno);
    return "Could not find bind target.";
  }

  // Create file / folder to which we will bind-mount source file / folder.
  if (S_ISDIR(sb.st_mode) != S_ISDIR(tb.st_mode)) {
    LOG(ERROR) << "Could not bind-mount " << source << " to " << target
               << ": types do not match (" << sb.st_mode << " != " << tb.st_mode
               << ")";
    return "Could not match source and target bind types.";
  }

  if (mount(source, target, NULL, MS_BIND, NULL) < 0) {
    LOG(ERROR) << "Could not bind " << source << " to " << target << ": "
               << strerror(errno);
    return "Could not bind item.";
  }

  LOG(INFO) << "Bound " << source << " -> " << target;

  return NULL;
}

const char* Container::ApplyCustomInit() {
  const char* custom_init_file =
      reader_->GetValueForKey(GceMetadataAttributes::kCustomInitFileKey);
  if (!custom_init_file) {
    custom_init_file = "";
  }

  AutoCloseFileDescriptor init_fd(
      open(kCustomInitFileName, O_CREAT | O_TRUNC | O_WRONLY, 0650));

  if (init_fd.IsError()) {
    LOG(ERROR) << "Could not create custom init file " << kCustomInitFileName
               << ": " << strerror(errno);
  } else {
    size_t sz = strlen(custom_init_file);
    ssize_t written = TEMP_FAILURE_RETRY(write(init_fd, custom_init_file, sz));

    if (written == -1) {
      LOG(WARNING) << "Warning: write failed on " << kCustomInitFileName;
    } else if (static_cast<size_t>(written) != sz) {
      LOG(WARNING) << "Warning: short write to " << kCustomInitFileName
                   << ", wanted " << sz << ", got " << written << ": "
                   << strerror(errno);
    } else {
      LOG(INFO) << "Custom init file created. Wrote " << written << " bytes to "
                << kCustomInitFileName;
    }
  }
  return NULL;
}

const char* Container::ApplyMetadataProperties() {
  avd::DisplayProperties display;
  const char* metadata_value =
      reader_->GetValueForKey(GceMetadataAttributes::kDisplayConfigurationKey);
  display.Parse(metadata_value);
  if (!metadata_value) {
    LOG(ERROR) << "No display configuration specified. Using defaults.";
  } else if (display.IsDefault()) {
    LOG(ERROR) << "Bad display value ignored " << metadata_value
               << ". Using default.";
  }
  AutoFreeBuffer metadata_properties;
  metadata_properties.PrintF(
      "on early-init\n"
      "  setprop ro.sf.lcd_density %d\n"
      "  setprop ro.hw.headless.display %s\n",
      display.GetDpi(), display.GetConfig());
  AutoCloseFileDescriptor init_fd(
      open(kMetadataPropertiesFileName, O_CREAT | O_TRUNC | O_WRONLY, 0650));

  if (init_fd.IsError()) {
    LOG(ERROR) << "Could not create metadata properties file "
               << kMetadataPropertiesFileName << ": " << strerror(errno);
  } else {
    ssize_t written = TEMP_FAILURE_RETRY(
        write(init_fd, metadata_properties.data(), metadata_properties.size()));
    if (static_cast<size_t>(written) != metadata_properties.size()) {
      LOG(WARNING) << "Warning: short write to " << kMetadataPropertiesFileName
                   << ", wanted " << metadata_properties.size() << ", got "
                   << written << ": " << strerror(errno);
    } else {
      LOG(INFO) << "Metadata properties created. Wrote " << written
                << " bytes to " << kMetadataPropertiesFileName;
    }
  }
  return NULL;
}

const char* Container::ApplyCustomization() {
  const char* res;

  // Check if we're booting mobile device. If so - initialize mobile network.
  std::map<std::string, std::string> target_properties;
  if (!avd::LoadPropertyFile("/target/system/build.prop", &target_properties)) {
    return "Failed to load property file /target/system/build.prop.";
  }

  const std::string& rild_path = target_properties["rild.libpath"];
  if (rild_path.empty()) {
    device_type_ = kDeviceTypeWifi;
  } else {
    device_type_ = kDeviceType3G;
  }

  // Create custom init.rc file from metadata.
  res = ApplyCustomInit();
  if (res) return res;

  // Create properties based on the metadata.
  res = ApplyMetadataProperties();
  if (res) return res;

  res = BindFiles();
  if (res) return res;

  if (device_type_ == kDeviceType3G) {
    res = ConfigureNetworkMobile();
    if (res) return res;
  }

  // Run gce_mount_hander before switching /system partitions.
  // The properties setup will no longer be valid after the switch, and that
  // can cause libc crashes on KitKat.
  // We can't link this code in here because it depends on libext4_utils, and
  // we can't have shared library dependencies in /init.
  if (!is_cuttlefish_) {
    LOG(INFO) << "Launching mount handler...";
    if (TEMP_FAILURE_RETRY(system("/system/bin/gce_mount_handler")) == -1) {
      LOG(ERROR) << "gce_mount_handler failed: " << strerror(errno);
      return "Could not start gce_mount_handler.";
    }
  } else {
    // TODO(ender): we should be able to merge gce_mount_handler with gce_init
    // shortly. Make sure that while booting cuttlefish we do launch
    // gce_mount_handler, too.
    avd::SharedFD file(
        avd::SharedFD::Open("/target/fstab.vsoc", O_RDWR | O_CREAT, 0640));
    const char fstab_data[] =
        "/dev/block/vdc /data ext4 nodev,noatime,nosuid,errors=panic wait\n";
    const char fstab_cache[] =
        "/dev/block/vdd /cache ext4 nodev,noatime,nosuid,errors=panic wait\n";

    file->Write(fstab_data, sizeof(fstab_data) - 1);
    file->Write(fstab_cache, sizeof(fstab_cache) - 1);

    avd::SharedFD ts_empty(
        avd::SharedFD::Open("/target/ts_snap.txt", O_RDWR | O_CREAT, 0444));
  }

  CopyFile("/initial.metadata", "/target/initial.metadata");

  return NULL;
}

const char* Container::CleanUp() {
  if (chdir("/target")) return "Could not chdir to /target.";

  // New filesystem does not have critical folders initialized.
  // Only bind-mount them here for the sake of mount_handler.
  // Shouldn't be needed once the mount handler is integrated in init.
  // "Hello, Container!"
  if (mount("/var", "/target/var", NULL, MS_MOVE, NULL))
    return "Could not bind /var.";

  // Unmount everything.
  if (umount("/system")) return "Could not unmount /system.";
  if (umount2("/proc", MNT_DETACH)) return "Could not unmount /proc.";
  if (umount2("/sys", MNT_DETACH)) return "Could not unmount /sys.";
  if (umount2("/dev", MNT_DETACH)) return "Could not unmount /dev.";

  // Abandon current root folder.
  // If we don't do it, we won't be able to re-mount root.
  if (mount(".", "/", NULL, MS_MOVE, NULL)) return "Could not move /.";
  if (chroot(".")) return "Could not chroot to '.'.";

  // Do not execute anything here any more.
  // Environment is empty and must be initialized by android's init process.
  // Set any open file descriptors to close on exec.
  for (int i = 3; i < 1024; ++i) {
    fcntl(i, F_SETFD, FD_CLOEXEC);
  }

  return NULL;
}

const char* Container::PivotToNamespace(const char* name) {
  if (!ns_manager_->SwitchNamespace(name))
    return "Could not pivot to a different namespace.";

  return NULL;
}

int main() {
  Container container;
  std::stringstream reason;

  google::InitGoogleLogging("Cuttlefish");
  google::LogToStderr();
  google::InstallFailureSignalHandler();

  LOG(INFO) << "Booting Cuttlefish.";

  if (!Init(&container, &reason)) {
    LOG(ERROR) << "VIRTUAL_DEVICE_BOOT_FAILED: " << reason.str().data();
    // If for some reason, however, Init completes, launch an emergency shell to
    // allow diagnosing what happened.
    if (system(kEmergencyShell))
      LOG(ERROR) << "Could not start emergency shell.";
    pause();
  }
}
