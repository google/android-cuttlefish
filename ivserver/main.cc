#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <memory>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "host/ivserver/ivserver.h"
#include "host/ivserver/options.h"

DEFINE_string(layout, "", "Location of the vsoc_mem.json file.");
DEFINE_string(mempath, "/dev/shm/ivshmem",
              "Target location for the shmem file.");
DEFINE_int32(shmsize, 4, "Size of the shared memory region in megabytes.");
DEFINE_string(qemusocket, "/tmp/ivshmem_socket_qemu", "QEmu socket path");
DEFINE_string(clientsocket, "/tmp/ivshmem_socket_client", "Client socket path");

namespace {
Json::Value LoadLayoutFile(const std::string &file) {
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
}  // anonymous namespace

int main(int argc, char **argv) {
  google::InstallFailureSignalHandler();
  google::ParseCommandLineFlags(&argc, &argv, true);

  std::unique_ptr<ivserver::IVServerOptions> ivserver_options(
      new ivserver::IVServerOptions(FLAGS_layout, FLAGS_mempath,
                                    FLAGS_qemusocket, FLAGS_clientsocket,
                                    FLAGS_shmsize));

  Json::Value json_root = LoadLayoutFile(FLAGS_layout);
  ivserver::IVServer ivserver(*ivserver_options, json_root);
  ivserver.Serve();
  LOG(FATAL) << "ivserver failed in Serve().";
}
