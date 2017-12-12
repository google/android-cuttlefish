#include "host/ivserver/ivserver.h"
#include "host/ivserver/options.h"
#include "host/ivserver/utils.h"

#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory>

#include <glog/logging.h>

#define LOG_TAG "ivserver_main"

static void usage(const char *program_name) {
  LOG(INFO) << "Usage: " << program_name << " [options]\n\n"
            << "Options:\n"
            << "  -l <vsoc_mem.json>\n"
            << "  --layout=<vsoc_mem.json>\n"
            << "     default is vsoc_mem.json\n\n"
            << "  -m <shm_file_path>\n"
            << "  --mempath=<shm_file_path>\n"
            << "     default is /dev/shm/ivshmem\n\n"
            << "  -s <shm_size_in_MiB>\n"
            << "  --shmsize=<shm_size_in_MiB>\n"
            << "     default is 4MiB\n\n"
            << "  -u <qemu_socket_path>\n"
            << "  --qemusocket=<qemu_socket_path>\n"
            << "     default is /tmp/ivshmem_socket\n\n"
            << "  -c <client_socket_path>\n"
            << "  --clientsocket=<client_socket_path>\n"
            << "     default is /tmp/ivshmem_socket_client\n\n"
            << "  -h\n"
            << "  --help\n";
  return;
}

static std::unique_ptr<ivserver::IVServerOptions> parse_options(int argc,
                                                                char **argv) {
  enum OptionValues {
    kLayoutConf,
    kSharedMemFileName,
    kSharedMemSizeMiB,
    kQemuSocket,
    kClientSocket,
    kHelp,
  };

  const struct option options[] = {
      {"layout", required_argument, nullptr, kLayoutConf},
      {"mempath", required_argument, nullptr, kSharedMemFileName},
      {"shmsize", required_argument, nullptr, kSharedMemSizeMiB},
      {"qemusocket", required_argument, nullptr, kQemuSocket},
      {"clientsocket", required_argument, nullptr, kClientSocket},
      {"help", no_argument, nullptr, kHelp},
      {0, 0, nullptr, 0}};

  int opt_index = 0;
  int current_option_char;

  const char *layout_file = ivserver::kIVServerDefaultLayoutFile.c_str();
  const char *shared_mem_file = ivserver::kIVServerDefaultShmFile.c_str();
  const char *qemusocket_path =
      ivserver::kIVServerDefaultQemuSocketPath.c_str();
  const char *clientsocket_path =
      ivserver::kIVServerDefaultClientSocketPath.c_str();
  uint32_t shared_mem_size_mib = ivserver::kIVServerDefaultShmSizeInMiB;

  //
  // Let us print error messages on command line errors.
  //
  opterr = 0;

  char *nextarg = argv[optind];
  while ((current_option_char = getopt_long(argc, argv, "+:l:m:s:u:c:hW;",
                                            options, &opt_index)) != -1) {
    switch (current_option_char) {
      case 'l':
      case kLayoutConf:
        layout_file = optarg;
        break;

      case 'm':
      case kSharedMemFileName:
        shared_mem_file = optarg;
        break;

      case 's':
      case kSharedMemSizeMiB:
        shared_mem_size_mib = atoi(optarg);
        if (shared_mem_size_mib <= 0) {
          LOG(ERROR) << "Invalid shared memory size.";
          usage(argv[0]);
          return nullptr;
        }
        break;

      case 'u':
      case kQemuSocket:
        qemusocket_path = optarg;
        break;

      case 'c':
      case kClientSocket:
        clientsocket_path = optarg;
        break;

      case 'h':
      case kHelp:
        usage(argv[0]);
        exit(0);
        break;

      case ':':
        LOG(ERROR) << "Missing argument after " << nextarg;
        usage(argv[0]);
        return nullptr;

      case '?':
        LOG(ERROR) << "Unknown option : " << nextarg;
        usage(argv[0]);
        return nullptr;

      default:
        usage(argv[0]);
        return nullptr;
    }

    nextarg = argv[optind];
  }

  std::unique_ptr<ivserver::IVServerOptions> ivserverOptions(
      new ivserver::IVServerOptions(layout_file, shared_mem_file,
                                    qemusocket_path, clientsocket_path,
                                    shared_mem_size_mib));
  return ivserverOptions;
}

int main(int argc, char **argv) {
  auto ivserver_options = parse_options(argc, argv);
  if (!ivserver_options) {
    LOG(FATAL) << "Error parsing options.";
    return 1;
  }

  LOG(INFO) << "Running ivserver with the following options: "
            << *ivserver_options;

  std::string real_path_to_mem_conf;
  bool status;

  status = ivserver::RealPath(ivserver_options->memory_layout_conf_path,
                              &real_path_to_mem_conf);
  if (!status) {
    LOG(FATAL) << "Failed in RealPath for "
               << ivserver_options->memory_layout_conf_path;
  }

  Json::Value json_root;
  status = ivserver::JsonInit(real_path_to_mem_conf, &json_root);
  if (!status) {
    LOG(FATAL) << "Failed in JsonInit for " << real_path_to_mem_conf;
  }

  ivserver::IVServer ivserver(*ivserver_options, json_root);
  if (!ivserver.HasInitialized()) {
    LOG(FATAL) << "ivserver initialization failed.";
    return 1;
  }

  ivserver.Serve();

  LOG(FATAL) << "ivserver failed in Serve().";

  return 1;
}
