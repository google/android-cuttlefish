/*
**
** Copyright 2018, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <android-base/logging.h>
#include <android/hardware/keymaster/4.1/IKeymasterDevice.h>
#include <cutils/properties.h>
#include <gflags/gflags.h>
#include <hidl/HidlTransportSupport.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/keymaster_channel.h"
#include <guest/hals/keymaster/remote/remote_keymaster.h>
#include <guest/hals/keymaster/remote/remote_keymaster4_device.h>

const char device[] = "/dev/hvc3";

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, ::android::base::KernelLogger);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  ::android::hardware::configureRpcThreadpool(1, true);

  LOG(INFO) << "Starting keymaster service4";

  auto fd = cuttlefish::SharedFD::Open(device, O_RDWR);
  if (!fd->IsOpen()) {
    LOG(FATAL) << "Could not connect to keymaster: " << fd->StrError();
  }

  if (fd->SetTerminalRaw() < 0) {
    LOG(FATAL) << "Could not make " << device << " a raw terminal: "
                << fd->StrError();
  }

  cuttlefish::SharedFdKeymasterChannel keymasterChannel(fd, fd);

  auto remoteKeymaster = new keymaster::RemoteKeymaster(&keymasterChannel);

  if (!remoteKeymaster->Initialize()) {
    LOG(FATAL) << "Could not initialize keymaster";
  }

  auto keymaster = new ::keymaster::V4_1::RemoteKeymaster4Device(remoteKeymaster);

  auto status = keymaster->registerAsService();
  if (status != android::OK) {
    LOG(FATAL) << "Could not register service for Keymaster 4.1 (" << status << ")";
    return -1;
  }

  android::hardware::joinRpcThreadpool();
  return -1;  // Should never get here.
}
