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

#include <string.h>

#include <fstream>
#include <sstream>
#include <string>

#include <cutils/properties.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"

DEFINE_uint32(port, property_get_int32("ro.boot.vsock_logcat_port", 0),
              "VSOCK port to send logcat output to");
DEFINE_uint32(cid, 2, "VSOCK CID to send logcat output to");

namespace {

class ServiceStatus {
 public:
  static const char* kServiceStatusProperty;
  static const char* kStatusStarted;
  static const char* kStatusFailed;

  ServiceStatus() {
    // This can fail if the property isn't set (the first time it runs), so
    // ignore the result.
    property_get(kServiceStatusProperty, status_, kStatusStarted);
  }

  bool Set(const char* status) {
    auto ret = property_set(kServiceStatusProperty, status);

    if (ret == 0) {
      strcpy(status_, status);
      return true;
    }
    return false;
  }

  const char* Get() { return status_; }

 private:
  char status_[PROP_VALUE_MAX];
};

const char* ServiceStatus::kServiceStatusProperty = "vendor.vsock_logcat_status";
const char* ServiceStatus::kStatusStarted = "started";
const char* ServiceStatus::kStatusFailed = "failed";

void LogFailed(const std::string& msg, ServiceStatus* status) {
  // Only log if status is not failed, ensuring it logs once per fail.
  if (strcmp(status->Get(), ServiceStatus::kStatusFailed) != 0) {
    LOG(ERROR) << msg;
    std::ofstream kmsg;
    kmsg.open("/dev/kmsg");
    kmsg << LOG_TAG << ": " << msg;
    kmsg << "";
    kmsg.close();
  }
  auto ret = status->Set(ServiceStatus::kStatusFailed);
  if (!ret) {
    LOG(ERROR) << "Unable to set value of property: "
               << ServiceStatus::kServiceStatusProperty;
  }
}
}  // namespace

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(FLAGS_port != 0) << "Port flag is required";

  ServiceStatus status;

  auto log_fd = cvd::SharedFD::VsockClient(FLAGS_cid, FLAGS_port, SOCK_STREAM);
  if (!log_fd->IsOpen()) {
    std::ostringstream msg;
    msg << "Unable to connect to vsock:" << FLAGS_cid << ":" << FLAGS_port
        << ": " << log_fd->StrError();
    LogFailed(msg.str(), &status);
    return 1;
  }
  auto ret = status.Set(ServiceStatus::kStatusStarted);
  if (!ret) {
    LOG(ERROR) << "Unable to set value of property: "
               << ServiceStatus::kServiceStatusProperty;
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
      std::ostringstream ss;
      ss << exit_msg << std::endl
         << "Unable to write complete message on socket: "
         << log_fd->StrError();
      LogFailed(ss.str(), &status);
      return 2;
    }
  }
}
