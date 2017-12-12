#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <sstream>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_select.h"
#include "host/libs/config/file_partition.h"
#include "host/libs/config/guest_config.h"
#include "host/libs/ivserver/ivserver.h"
#include "host/libs/ivserver/options.h"
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

DEFINE_int32(instance, 1, "Instance number. Must be unique.");
DEFINE_string(mobile_interface, "abr0",
              "Network interface to use for mobile networking");
DEFINE_int32(cpus, 2, "Virtual CPU count.");
DEFINE_string(launch_command, "virsh create /dev/fd/0",
              "Command to start an instance");
DEFINE_int32(memory_mb, 2048,
             "Total amount of memory available for guest, MB.");
DEFINE_string(layout,
              StringFromEnv("ANDROID_HOST_OUT", StringFromEnv("HOME", ".")) +
                  "/config/vsoc_mem.json",
              "Location of the vsoc_mem.json file.");
DEFINE_string(mempath, "/dev/shm/ivshmem",
              "Target location for the shmem file.");
DEFINE_int32(shmsize, 0, "(ignored)");
DEFINE_string(qemusocket, "/tmp/ivshmem_socket_qemu", "QEmu socket path");
DEFINE_string(clientsocket, "/tmp/ivshmem_socket_client", "Client socket path");

DEFINE_string(system_image_dir,
              StringFromEnv("ANDROID_PRODUCT_OUT", StringFromEnv("HOME", ".")),
              "Location of the system partition images.");
DEFINE_string(kernel, "", "Location of cuttlefish kernel file.");
DEFINE_string(kernel_command_line, "",
              "Location of a text file with the kernel command line.");
DEFINE_string(initrd, "", "Location of cuttlefish initrd file.");
DEFINE_string(data_image, "", "Location of the data partition image.");
DEFINE_string(cache_image, "", "Location of the cache partition image.");
DEFINE_string(vendor_image, "", "Location of the vendor partition image.");

DEFINE_string(usbipsocket, "android_usbip", "Name of the USB/IP socket.");
DEFINE_bool(log_xml, false, "Log the XML machine configuration");

namespace {
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
  VirtualUSBManager(const std::string& usbsocket)
      : adb_{usbsocket, FLAGS_usbipsocket},
        usbip_{FLAGS_usbipsocket, adb_.Pool()} {}

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
      avd::SharedFDSet fd_read;
      fd_read.Zero();

      adb_.BeforeSelect(&fd_read);
      usbip_.BeforeSelect(&fd_read);

      int ret = avd::Select(&fd_read, nullptr, nullptr, nullptr);
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
                                          FLAGS_qemusocket, FLAGS_clientsocket),
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

}  // anonymous namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  LOG_IF(FATAL, FLAGS_system_image_dir.empty())
      << "--system_image_dir must be specified.";

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

  std::ifstream t(FLAGS_kernel_command_line);
  if (!t) {
    LOG(FATAL) << "Unable to open " << FLAGS_kernel_command_line;
  }
  t.seekg(0, std::ios::end);
  size_t commandline_size = t.tellg();
  if (commandline_size < 1) {
    LOG(FATAL) << "no command line data found at " << FLAGS_kernel_command_line;
  }
  std::string cmdline;
  cmdline.reserve(commandline_size);
  t.seekg(0, std::ios::beg);
  cmdline.assign((std::istreambuf_iterator<char>(t)),
                 std::istreambuf_iterator<char>());
  t.close();

  std::string entropy_source = "/dev/urandom";

  config::GuestConfig cfg;
  cfg.SetID(FLAGS_instance)
      .SetVCPUs(FLAGS_cpus)
      .SetMemoryMB(FLAGS_memory_mb)
      .SetKernelName(FLAGS_kernel)
      .SetInitRDName(FLAGS_initrd)
      .SetKernelArgs(cmdline)
      .SetIVShMemSocketPath(FLAGS_qemusocket)
      .SetIVShMemVectorCount(json_root["vsoc_device_regions"].size())
      .SetSystemPartitionPath(system_partition->GetName())
      .SetCachePartitionPath(cache_partition->GetName())
      .SetDataPartitionPath(data_partition->GetName())
      .SetVendorPartitionPath(vendor_partition->GetName())
      .SetMobileBridgeName(FLAGS_mobile_interface)
      .SetEntropySource(entropy_source);
  cfg.SetUSBV1SocketName(std::string("/tmp/") + cfg.GetInstanceName() +
                         "-usb");

  std::string xml = cfg.Build();
  if (FLAGS_log_xml) {
    LOG(INFO) << "Using XML:\n" << xml;
  }

  VirtualUSBManager vadb(cfg.GetUSBV1SocketName());
  vadb.Start();
  IVServerManager ivshmem(json_root);
  ivshmem.Start();

  sleep(1);

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
