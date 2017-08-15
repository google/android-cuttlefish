#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <sstream>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <libvirt/libvirt.h>

#include "common/libs/fs/shared_select.h"
#include "host/ivserver/ivserver.h"
#include "host/ivserver/options.h"
#include "host/launcher/file_partition.h"
#include "host/launcher/guest_config.h"
#include "host/vadb/usbip/server.h"
#include "host/vadb/virtual_adb_server.h"

DEFINE_int32(instance, 1, "Instance number. Must be unique.");
DEFINE_int32(cpus, 4, "Virtual CPU count.");
DEFINE_int32(memory_mb, 1024, "Total amount of memory available for guest, MB.");

DEFINE_string(layout, "", "Location of the vsoc_mem.json file.");
DEFINE_string(mempath, "/dev/shm/ivshmem",
              "Target location for the shmem file.");
DEFINE_int32(shmsize, 4, "Size of the shared memory region in megabytes.");
DEFINE_string(qemusocket, "/tmp/ivshmem_socket_qemu", "QEmu socket path");
DEFINE_string(clientsocket, "/tmp/ivshmem_socket_client", "Client socket path");
DEFINE_string(system_image_dir, "", "Location of the system partition images.");
DEFINE_string(initrd, "", "Location of cuttlefish initrd file.");
DEFINE_string(kernel, "", "Location of cuttlefish kernel file.");

DEFINE_string(usbipsocket, "android_usbip", "Name of the USB/IP socket.");

namespace {
constexpr char kLibVirtQemuTarget[] = "qemu:///system";

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
                                          FLAGS_qemusocket, FLAGS_clientsocket,
                                          FLAGS_shmsize),
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
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  google::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(virInitialize() == 0) << "Could not initialize libvirt.";

  Json::Value json_root = LoadLayoutFile(FLAGS_layout);

  // Each of these calls is free to fail and terminate launch if file does not
  // exist or could not be created.
  auto ramdisk_partition = launcher::FilePartition::ReuseExistingFile(
      FLAGS_system_image_dir + "/ramdisk.img");
  auto system_partition = launcher::FilePartition::ReuseExistingFile(
      FLAGS_system_image_dir + "/system.img");
  auto data_partition =
      launcher::FilePartition::CreateTemporaryFile("/tmp/cf-data", 512);
  auto cache_partition =
      launcher::FilePartition::CreateTemporaryFile("/tmp/cf-cache", 512);
  auto kernel_image = launcher::FilePartition::ReuseExistingFile(FLAGS_kernel);
  auto initrd_image = launcher::FilePartition::ReuseExistingFile(FLAGS_initrd);

  std::stringstream cmdline;
  for (const auto& value : json_root["guest"]["kernel_command_line"]) {
    cmdline << value.asString() << ' ';
  }

  unsigned long libvirt_version;
  CHECK(virGetVersion(&libvirt_version, nullptr, nullptr) == 0)
      << "Could not query libvirt.";

  std::string entropy_source = "/dev/urandom";
  // There seems to be no macro turning major/minor/patch to a number, but
  // headers explain this as major * 1'000'000 + minor * 1'000 + patch.
  if (libvirt_version <= 1003003) {
    entropy_source = "/dev/random";
    LOG(WARNING) << "Your system supplies old version of libvirt, that is "
                 << "not able to use /dev/urandom as entropy source.";
    LOG(WARNING) << "This may affect performance of your virtual instance.";
  }

  launcher::GuestConfig cfg;
  cfg.SetID(FLAGS_instance)
      .SetVCPUs(FLAGS_cpus)
      .SetMemoryMB(FLAGS_memory_mb)
      .SetKernelName(kernel_image->GetName())
      .SetInitRDName(initrd_image->GetName())
      .SetKernelArgs(cmdline.str())
      .SetIVShMemSocketPath(FLAGS_qemusocket)
      .SetIVShMemVectorCount(json_root["vsoc_device_regions"].size())
      .SetRamdiskPartitionPath(ramdisk_partition->GetName())
      .SetSystemPartitionPath(system_partition->GetName())
      .SetCachePartitionPath(cache_partition->GetName())
      .SetDataPartitionPath(data_partition->GetName())
      .SetMobileBridgeName("abr0")
      .SetEntropySource(entropy_source)
      .SetEmulator(json_root["guest"]["vmm_path"].asString());

  std::string xml = cfg.Build();
  VLOG(1) << "Using XML:\n" << xml;

  auto libvirt_connection = virConnectOpen(kLibVirtQemuTarget);
  CHECK(libvirt_connection)
      << "Could not connect to libvirt backend: " << kLibVirtQemuTarget;

  VirtualUSBManager vadb(cfg.GetUSBSocketName());
  vadb.Start();
  IVServerManager ivshmem(json_root);
  ivshmem.Start();

  sleep(1);

  auto domain = virDomainCreateXML(libvirt_connection, xml.c_str(),
                                   VIR_DOMAIN_START_PAUSED |
                                   VIR_DOMAIN_START_AUTODESTROY);
  CHECK(domain) << "Could not create libvirt domain.";

  CHECK(virDomainResume(domain) == 0) << "Could not start domain.";
  pause();
}
