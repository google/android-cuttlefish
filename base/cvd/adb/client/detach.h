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

#pragma once

#include <memory>
#include <string>
#include <unordered_set>

#include "transport.h"

// If an adb server uses ADB_LIBUSB_START_DETACHED, all devices started detached. But we need a way
// to tell if this setting should be overridden when a device is attached and then intentionally
// disconnected and then reconnected (which can happen via `adb reboot` or `adb root/unroot`).
class AttachedDevices {
  public:
    void RegisterAttach(const std::string& serial);

    void RegisterDetach(const std::string& serial);

    bool ShouldStartDetached(const Connection& connection);

  private:
    bool IsAttached(const std::string& serial);

    std::mutex attached_devices_mutex_;

    // Stores serial numbers of all devices which have been attached.
    // Entries are cleared when a device is detached.
    std::unordered_set<std::string> attached_devices_ GUARDED_BY(attached_devices_mutex_);
};

extern AttachedDevices attached_devices;