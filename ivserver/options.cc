#include "host/ivserver/options.h"

namespace ivserver {

IVServerOptions::IVServerOptions(const std::string &mem_layout_conf,
                                 const std::string &shm_file_path,
                                 const std::string &qemu_socket_path,
                                 const std::string &client_socket_path,
                                 const uint32_t shm_size_mib)
    : memory_layout_conf_path(mem_layout_conf),
      shm_file_path(shm_file_path),
      qemu_socket_path(qemu_socket_path),
      client_socket_path(client_socket_path),
      shm_size_mib(shm_size_mib) {}

std::ostream &operator<<(std::ostream &out, const IVServerOptions &options) {
  out << "\nmem_layout_conf_path: " << options.memory_layout_conf_path
      << "\nshm_file: " << options.shm_file_path
      << "\nshm_size_MiB: " << options.shm_size_mib
      << "\nqemu_socket_path: " << options.qemu_socket_path
      << "\nclient_socket_path: " << options.client_socket_path << std::endl;

  return out;
}

}  // namespace ivserver
