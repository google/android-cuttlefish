/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "detach.h"

#include "usb.h"

AttachedDevices attached_devices [[clang::no_destroy]];

void AttachedDevices::RegisterAttach(const std::string& serial) {
    std::lock_guard<std::mutex> lock(attached_devices_mutex_);
    attached_devices_.insert(serial);
}

void AttachedDevices::RegisterDetach(const std::string& serial) {
    std::lock_guard<std::mutex> lock(attached_devices_mutex_);
    attached_devices_.erase(serial);
}

bool AttachedDevices::IsAttached(const std::string& serial) {
    std::lock_guard<std::mutex> lock(attached_devices_mutex_);
    return attached_devices_.contains(serial);
}

bool AttachedDevices::ShouldStartDetached(const Connection& c) {
    if (!c.SupportsDetach()) {
        return false;
    }
    static const char* env = getenv("ADB_LIBUSB_START_DETACHED");
    static bool should_start_detached = env && strcmp("1", env) == 0;
    return should_start_detached && !IsAttached(c.Serial());
}