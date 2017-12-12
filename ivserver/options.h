#pragma once

#include <inttypes.h>
#include <iostream>
#include <string>

namespace ivserver {

const uint16_t kIVServerMajorVersion = 1;
const uint16_t kIVServerMinorVersion = 0;
const uint32_t kIVServerDefaultShmSizeInMiB = 4;
const std::string kIVServerDefaultShmFile = "ivshmem";
const std::string kIVServerDefaultLayoutFile = "vsoc_mem.json";
const std::string kIVServerDefaultQemuSocketPath = "/tmp/ivshmem_socket";
const std::string kIVServerDefaultClientSocketPath =
    "/tmp/ivshmem_client_socket";

//
// structure that contains the various options to start the server.
//
struct IVServerOptions final {
  IVServerOptions(const std::string &mem_layout_conf,
                  const std::string &shm_file_path,
                  const std::string &qemu_socket_path,
                  const std::string &client_socket_path,
                  const uint32_t shm_size_mib);

  //
  // We still need a friend here
  //
  friend std::ostream &operator<<(std::ostream &out,
                                  const IVServerOptions &opts);

  const std::string memory_layout_conf_path;
  const std::string shm_file_path;
  const std::string qemu_socket_path;
  const std::string client_socket_path;
  const uint32_t shm_size_mib;
};

}  // namespace ivserver
