/*
 * Copyright (C) 2024 The Android Open Source Project
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

#define LOG_TAG "android.hardware.secure_element-service.jcardsim"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "SecureElement.h"

const char device[] = "/dev/hvc17";

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto fd = cuttlefish::SharedFD::Open(device, O_RDWR);
    if (!fd->IsOpen()) {
        LOG(FATAL) << "Could not connect to keymaster: " << fd->StrError();
    }

    if (fd->SetTerminalRaw() < 0) {
        LOG(FATAL) << "Could not make " << device << " a raw terminal: " << fd->StrError();
    }

    std::shared_ptr<cuttlefish::transport::SharedFdChannel> jcardsimChannel =
        std::make_shared<cuttlefish::transport::SharedFdChannel>(fd, fd);

    auto se = ndk::SharedRefBase::make<aidl::android::hardware::secure_element::SecureElement>(
        jcardsimChannel);
    const std::string name = std::string() +
                             aidl::android::hardware::secure_element::BnSecureElement::descriptor +
                             "/eSE1";
    binder_status_t status = AServiceManager_addService(se->asBinder().get(), name.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
