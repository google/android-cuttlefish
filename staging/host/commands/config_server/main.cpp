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

#include <common/libs/device_config/device_config.h>
#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/tee_logging.h"
#include "host/libs/config/logging.h"

DEFINE_int32(
    server_fd, -1,
    "File descriptor to an already created vsock server. Must be specified.");

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto device_config = cuttlefish::DeviceConfig::Get();

  CHECK(device_config) << "Could not open device config";

  cuttlefish::SharedFD server_fd = cuttlefish::SharedFD::Dup(FLAGS_server_fd);

  CHECK(server_fd->IsOpen()) << "Inheriting logcat server: "
                             << server_fd->StrError();

  // Server loop
  while (true) {
    auto conn = cuttlefish::SharedFD::Accept(*server_fd);
    LOG(DEBUG) << "Connection received on configuration server";

    bool succeeded = device_config->SendRawData(conn);
    if (succeeded) {
      LOG(DEBUG) << "Successfully sent device configuration";
    } else {
      LOG(ERROR) << "Failed to send the device configuration: "
                 << conn->StrError();
    }
  }
}
