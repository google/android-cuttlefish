/*
 * Copyright 2019 The Android Open Source Project
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

#include <hidl/HidlLazyUtils.h>
#include <hidl/HidlTransportSupport.h>
#include "Storage.h"

using android::OK;
using android::sp;
using android::status_t;
using android::UNKNOWN_ERROR;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::LazyServiceRegistrar;
using android::hardware::health::storage::V1_0::IStorage;
using android::hardware::health::storage::V1_0::implementation::Storage;

int main() {
    configureRpcThreadpool(1, true);

    sp<IStorage> service = new Storage();
    auto serviceRegistrar = LazyServiceRegistrar::getInstance();
    status_t result = serviceRegistrar.registerService(service);

    if (result != OK) {
        return result;
    }

    joinRpcThreadpool();
    return UNKNOWN_ERROR;
}
