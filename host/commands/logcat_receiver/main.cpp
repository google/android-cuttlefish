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

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"

DEFINE_int32(
    server_fd, -1,
    "File descriptor to an already created vsock server. Must be specified.");

int main(int argc, char** argv) {
  cvd::DefaultSubprocessLogging(argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = vsoc::CuttlefishConfig::Get();

  auto instance = config->ForDefaultInstance();
  auto path = instance.logcat_path();
  auto logcat_file =
      cvd::SharedFD::Open(path.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0666);
  CHECK(logcat_file->IsOpen())
      << "Unable to open logcat file: " << logcat_file->StrError();

  cvd::SharedFD server_fd = cvd::SharedFD::Dup(FLAGS_server_fd);
  close(FLAGS_server_fd);

  CHECK(server_fd->IsOpen()) << "Error creating or inheriting logcat server: "
                             << server_fd->StrError();

  // Server loop
  while (true) {
    auto conn = cvd::SharedFD::Accept(*server_fd);

    while (true) {
      char buff[1024];
      auto read = conn->Read(buff, sizeof(buff));
      if (read <= 0) {
        // Close here to ensure the other side gets reset if it's still
        // connected
        conn->Close();
        LOG(WARNING) << "Detected close from the other side";
        break;
      }
      auto written = logcat_file->Write(buff, read);
      CHECK(written == read)
          << "Error writing to log file: " << logcat_file->StrError()
          << ". This is unrecoverable.";
    }
  }
  return 0;
}
