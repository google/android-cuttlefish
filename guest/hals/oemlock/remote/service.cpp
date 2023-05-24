/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/channel_sharedfd.h"
#include "guest/hals/oemlock/remote/remote_oemlock.h"

using ::aidl::android::hardware::oemlock::OemLock;

int main(int argc, char *argv[]) {
    ::android::base::InitLogging(argv, ::android::base::KernelLogger);
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    if (argc != 2) {
        LOG(FATAL) << "Cuttlefish OemLock HAL requires to have hvc path as a first argument";
    }
    const auto fd = cuttlefish::SharedFD::Open(argv[1], O_RDWR);
    if (!fd->IsOpen()) {
        LOG(FATAL) << "Could not connect to oemlock: " << fd->StrError();
    }
    if (fd->SetTerminalRaw() < 0) {
        LOG(FATAL) << "Could not make " << argv[1] << " a raw terminal: " << fd->StrError();
    }

    cuttlefish::secure_env::SharedFdChannel channel(fd, fd);
    std::shared_ptr<OemLock> oemlock = ndk::SharedRefBase::make<OemLock>(channel);

    const std::string instance = std::string() + OemLock::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(oemlock->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return -1; // Should never be reached
}
