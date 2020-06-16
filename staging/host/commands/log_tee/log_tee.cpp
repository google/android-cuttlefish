//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/tee_logging.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_string(process_name, "", "The process to credit log messages to");
DEFINE_int32(log_fd_in, -1, "The file descriptor to read logs from.");

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, /* remove_flags */ true);

  CHECK(FLAGS_log_fd_in >= 0) << "-log_fd_in is required";

  auto config = vsoc::CuttlefishConfig::Get();

  CHECK(config) << "Could not open cuttlefish config";

  auto instance = config->ForDefaultInstance();

  if (config->run_as_daemon()) {
    android::base::SetLogger(
        cvd::LogToFiles({instance.launcher_log_path()}));
  } else {
    android::base::SetLogger(
        cvd::LogToStderrAndFiles({instance.launcher_log_path()}));
  }

  auto log_fd = cvd::SharedFD::Dup(FLAGS_log_fd_in);
  CHECK(log_fd->IsOpen()) << "Failed to dup log_fd_in: " <<  log_fd->StrError();
  close(FLAGS_log_fd_in);

  if (FLAGS_process_name.size() > 0) {
    android::base::SetDefaultTag(FLAGS_process_name);
  }

  char buf[1 << 16];
  ssize_t chars_read = 0;

  LOG(DEBUG) << "Starting to read from process " << FLAGS_process_name;

  while ((chars_read = log_fd->Read(buf, sizeof(buf))) > 0) {
    auto trimmed = android::base::Trim(std::string(buf, chars_read));
    // Newlines inside `trimmed` are handled by the android logging code.
    LOG(INFO) << trimmed;
  }

  if (chars_read < 0) {
    LOG(DEBUG) << "Failed to read from process " << FLAGS_process_name << ": "
               << log_fd->StrError();
  }

  LOG(DEBUG) << "Finished reading from process " << FLAGS_process_name;
}
