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

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>
#include <sstream>
#include <string>

#include <cutils/properties.h>
#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"

DEFINE_uint32(
    port,
    static_cast<uint32_t>(property_get_int64("ro.boot.vsock_logcat_port", 0)),
    "VSOCK port to send logcat output to");
DEFINE_uint32(cid, 2, "VSOCK CID to send logcat output to");
DEFINE_string(pipe_name, "/dev/cf_logcat_pipe",
              "The path for the named pipe logcat will write to");

namespace {

constexpr char kLogcatExitMsg[] = "\nDetected exit of logcat process\n\n";

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

  auto log_fd = cuttlefish::SharedFD::VsockClient(FLAGS_cid, FLAGS_port, SOCK_STREAM);
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

  if (cuttlefish::FileExists(FLAGS_pipe_name)) {
    LOG(WARNING) << "The file " << FLAGS_pipe_name << " already exists. Deleting...";
    cuttlefish::RemoveFile(FLAGS_pipe_name);
  }
  auto pipe = mkfifo(FLAGS_pipe_name.c_str(), 0600);
  if (pipe != 0) {
    LOG(FATAL) << "unable to create pipe: " << strerror(errno);
  }
  property_set("vendor.ser.cf-logcat", FLAGS_pipe_name.c_str());
  while (1) {
    auto conn = cuttlefish::SharedFD::Open(FLAGS_pipe_name.c_str(), O_RDONLY);
    while (conn->IsOpen()) {
      char buff[4096];
      auto read = conn->Read(buff, sizeof(buff));
      if (read) {
        log_fd->Write(buff, read);
      } else {
        conn->Close();
      }
    }
    log_fd->Write(kLogcatExitMsg, sizeof(kLogcatExitMsg) - 1);
  }
}
