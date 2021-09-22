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

#include <signal.h>

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"

DEFINE_int32(log_pipe_fd, -1,
             "A file descriptor representing a (UNIX) socket from which to "
             "read the logs. If -1 is given the socket is created according to "
             "the instance configuration");

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = cuttlefish::CuttlefishConfig::Get();

  CHECK(config) << "Could not open cuttlefish config";

  auto instance = config->ForDefaultInstance();

  // Disable default handling of SIGPIPE
  struct sigaction new_action {
  }, old_action{};
  new_action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &new_action, &old_action);

  cuttlefish::SharedFD pipe;
  if (FLAGS_log_pipe_fd < 0) {
    auto log_name = instance.logcat_pipe_name();
    pipe = cuttlefish::SharedFD::Open(log_name.c_str(), O_RDONLY);
  } else {
    pipe = cuttlefish::SharedFD::Dup(FLAGS_log_pipe_fd);
    close(FLAGS_log_pipe_fd);
  }

  if (!pipe->IsOpen()) {
    LOG(ERROR) << "Error opening log pipe: " << pipe->StrError();
    return 2;
  }

  auto path = instance.logcat_path();
  auto logcat_file =
      cuttlefish::SharedFD::Open(path.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0666);

  // Server loop
  while (true) {
    char buff[1024];
    auto read = pipe->Read(buff, sizeof(buff));
    if (read < 0) {
      LOG(ERROR) << "Could not read logcat: " << pipe->StrError();
      break;
    }
    auto written = cuttlefish::WriteAll(logcat_file, buff, read);
    CHECK(written == read)
        << "Error writing to log file: " << logcat_file->StrError()
        << ". This is unrecoverable.";
  }

  logcat_file->Close();
  pipe->Close();
  return 0;
}
