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
#define LOG_TAG "android.hardware.gatekeeper@1.0-service.remote"

#include <android-base/logging.h>
#include <android/hardware/gatekeeper/1.0/IGatekeeper.h>
#include <cutils/properties.h>
#include <gflags/gflags.h>

#include <hidl/LegacySupport.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/gatekeeper_channel.h"
#include "guest/hals/gatekeeper/remote/remote_gatekeeper.h"

// Generated HIDL files
using android::hardware::gatekeeper::V1_0::IGatekeeper;
using gatekeeper::RemoteGateKeeperDevice;

DEFINE_uint32(
    port,
    static_cast<uint32_t>(property_get_int64("ro.boot.vsock_gatekeeper_port", 0)),
    "virtio socket port to send gatekeeper commands to");

int main(int argc, char** argv) {
    ::android::base::InitLogging(argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::android::hardware::configureRpcThreadpool(1, true /* willJoinThreadpool */);

    auto vsockFd = cuttlefish::SharedFD::VsockClient(2, FLAGS_port, SOCK_STREAM);
    if (!vsockFd->IsOpen()) {
        LOG(FATAL) << "Could not connect to keymaster server: "
                   << vsockFd->StrError();
    }
    cuttlefish::GatekeeperChannel gatekeeperChannel(vsockFd);

    android::sp<RemoteGateKeeperDevice> gatekeeper(
        new RemoteGateKeeperDevice(&gatekeeperChannel));
    auto status = gatekeeper->registerAsService();
    if (status != android::OK) {
        LOG(FATAL) << "Could not register service for Gatekeeper 1.0 (remote) (" << status << ")";
    }

    android::hardware::joinRpcThreadpool();
    return -1;  // Should never get here.
}
