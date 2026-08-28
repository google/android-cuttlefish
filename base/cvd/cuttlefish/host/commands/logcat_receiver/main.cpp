/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gflags/gflags.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/host/libs/config/config_instance_derived.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/logging.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

DEFINE_int32(log_pipe_fd, -1,
             "A file descriptor representing a (UNIX) socket from which to "
             "read the logs. If -1 is given the socket is created according to "
             "the instance configuration");

namespace cuttlefish {
namespace {

Result<void> Main(int argc, char** argv) {
  DefaultSubprocessLogging(argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  const CuttlefishConfig::InstanceSpecific& instance =
      CF_EXPECT(CuttlefishConfig::Get())->ForDefaultInstance();

  // Disable default handling of SIGPIPE
  struct sigaction new_action{};
  new_action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &new_action, nullptr);

  SharedFD pipe;

  if (FLAGS_log_pipe_fd < 0) {
    pipe = CF_EXPECT(Fd::Open(LogcatPipeName(instance), O_RDONLY));
  } else {
    pipe = CF_EXPECT(Fd::Dup(FLAGS_log_pipe_fd));
    close(FLAGS_log_pipe_fd);
  }

  Fd logcat_file = CF_EXPECT(
      Fd::Open(LogcatPath(instance), O_CREAT | O_APPEND | O_WRONLY, 0666));

  // Server loop
  while (true) {
    char buff[1024];
    uint64_t data_read = CF_EXPECT(pipe->Read(buff, sizeof(buff)));
    CF_EXPECT(WriteExact(logcat_file, buff, data_read));
  }

  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  cuttlefish::Result<void> res = cuttlefish::Main(argc, argv);
  if (!res.has_value()) {
    LOG(ERROR) << res.error();
    return -1;
  }
  return 0;
}
