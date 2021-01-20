/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "Storage.h"

using aidl::android::hardware::health::storage::Storage;
using std::string_literals::operator""s;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    // make a default storage service
    auto storage = ndk::SharedRefBase::make<Storage>();
    const std::string name = Storage::descriptor + "/default"s;
    CHECK_EQ(STATUS_OK, AServiceManager_registerLazyService(
                            storage->asBinder().get(), name.c_str()));

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
