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

#include <unistd.h>

#include <thread>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/commands/ivserver/ivserver.h"
#include "host/commands/ivserver/options.h"

DEFINE_int32(
    qemu_socket_fd, -1,
    "A file descriptor to use as the server Qemu connects to. If not specified "
    "a unix socket will be created in the default location.");
DEFINE_int32(
    client_socket_fd, -1,
    "A file descriptor to use as the server clients connects to. If not "
    "specified a unix socket will be created in the default location.");

int main(int argc, char* argv[]) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = vsoc::CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Unable to get cuttlefish config";
    return 1;
  }

  ivserver::IVServer server(
      ivserver::IVServerOptions(config->mempath(),
                                config->ivshmem_qemu_socket_path(),
                                vsoc::GetDomain()),
      FLAGS_qemu_socket_fd, FLAGS_client_socket_fd);

  // Close the file descriptors as they have been dupped by now.
  if (FLAGS_qemu_socket_fd > 0) {
    close(FLAGS_qemu_socket_fd);
  }
  if (FLAGS_client_socket_fd > 0) {
    close(FLAGS_client_socket_fd);
  }

  server.Serve();

  return 0;
}
