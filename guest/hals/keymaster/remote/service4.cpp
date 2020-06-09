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

DEFINE_uint32(
    port,
    static_cast<uint32_t>(property_get_int64("ro.boot.vsock_keymaster_port", 0)),
    "virtio socket port to send keymaster commands to");

int main(int argc, char** argv) {
    ::android::base::InitLogging(argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::android::hardware::configureRpcThreadpool(1, true);

    auto vsockFd = cvd::SharedFD::VsockClient(2, FLAGS_port, SOCK_STREAM);
    if (!vsockFd->IsOpen()) {
        LOG(FATAL) << "Could not connect to keymaster server: "
                   << vsockFd->StrError();
    }
    cvd::KeymasterChannel keymasterChannel(vsockFd);
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
