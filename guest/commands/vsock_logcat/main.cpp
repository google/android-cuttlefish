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

#define LOG_TAG "vsock_logcat"

#include <sstream>

#include <cutils/properties.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"

DEFINE_uint32(port, property_get_int32("ro.boot.vsock_logcat_port", 0),
              "VSOCK port to send logcat output to");
DEFINE_uint32(cid, 2, "VSOCK CID to send logcat output to");

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(FLAGS_port != 0) << "Port flag is required";

  auto log_fd = cvd::SharedFD::VsockClient(FLAGS_cid, FLAGS_port, SOCK_STREAM);
  if (!log_fd->IsOpen()) {
    LOG(ERROR) << "Unable to connect to vsock:" << FLAGS_cid << ":"
               << FLAGS_port << ": " << log_fd->StrError();
    return 1;
  }

  cvd::Command logcat_cmd("/system/bin/logcat");
  logcat_cmd.AddParameter("-b");
  logcat_cmd.AddParameter("all");
  logcat_cmd.AddParameter("-v");
  logcat_cmd.AddParameter("threadtime");
  logcat_cmd.AddParameter("*:V");

  logcat_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut, log_fd);

  while (true) {
    int wstatus;
    logcat_cmd.Start().Wait(&wstatus, 0);
    std::ostringstream exit_msg_builder;
    exit_msg_builder << std::endl
                     << "Logcat process exited with wstatus " << wstatus
                     << ", restarting ..." << std::endl
                     << std::endl;
    auto exit_msg = exit_msg_builder.str();
    size_t written = log_fd->Write(exit_msg.c_str(), exit_msg.size());
    if (written != exit_msg.size()) {
      LOG(ERROR) << "Unable to write complete message on socket: "
                 << log_fd->StrError();
      return 2;
    }
  }
}
