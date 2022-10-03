/*
 * Copyright 2020, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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

#include "TrustyConfirmationUI.h"

using ::aidl::android::hardware::confirmationui::TrustyConfirmationUI;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<TrustyConfirmationUI> confirmationui =
        ndk::SharedRefBase::make<TrustyConfirmationUI>();

    const std::string instance = std::string() + TrustyConfirmationUI::descriptor + "/default";
    binder_status_t status =
        AServiceManager_addService(confirmationui->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    if (status != STATUS_OK) {
        LOG(FATAL) << "Could not register service for ConfirmationUI 1.0 (" << status << ")";
        return -1;
    }

    ABinderProcess_joinThreadPool();
    return -1;  // Should never get here.
}
