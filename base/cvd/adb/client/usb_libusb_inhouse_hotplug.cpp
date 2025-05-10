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

#include "usb_libusb_inhouse_hotplug.h"

#include <chrono>
#include <thread>
#include <unordered_map>

#include "adb_trace.h"
#include "client/usb_libusb_hotplug.h"

#include "libusb/libusb.h"

namespace libusb_inhouse_hotplug {

class ScanRateLimiter {
  public:
    ScanRateLimiter(std::chrono::seconds rate) : rate_s_(rate) { Tick(); }
    bool Exceeded() {
        auto elapsed_since_last_scan = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - last_tick_);
        return elapsed_since_last_scan < rate_s_;
    }

    void Tick() { last_tick_ = std::chrono::steady_clock::now(); }

  private:
    std::chrono::seconds rate_s_;
    std::chrono::time_point<std::chrono::steady_clock> last_tick_;
};

std::chrono::seconds kScan_rate_s = std::chrono::seconds(2);
static ScanRateLimiter rate_limiter{kScan_rate_s};

// We need to synchronize access to the list of known devices. It can be modified from both the
// monitoring thread but also LibUsbConnection threads (when they report being closed).
static std::mutex known_devices_mutex [[clang::no_destroy]];
static std::unordered_map<uint64_t, libusb_device*> GUARDED_BY(known_devices_mutex) known_devices
        [[clang::no_destroy]];

void scan() {
    if (rate_limiter.Exceeded()) {
        return;
    }
    rate_limiter.Tick();

    VLOG(USB) << "inhouse USB scanning";
    std::lock_guard<std::mutex> lock(known_devices_mutex);

    // First retrieve all connected devices and detect new ones.
    libusb_device** devs = nullptr;
    libusb_get_device_list(nullptr, &devs);
    std::unordered_map<uint64_t, libusb_device*> current_devices;
    for (size_t i = 0; devs[i] != nullptr; i++) {
        libusb_device* dev = devs[i];
        auto session_id = LibUsbDevice::GenerateSessionId(dev).id;
        if (!known_devices.contains(session_id) && !current_devices.contains(session_id)) {
            hotplug_callback(nullptr, dev, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, nullptr);
        }
        current_devices[session_id] = dev;
    }

    // Handle disconnected devices
    for (const auto& [session_id, dev] : known_devices) {
        if (!current_devices.contains(session_id)) {
            hotplug_callback(nullptr, dev, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, nullptr);
        }
    }
    known_devices = std::move(current_devices);
    libusb_free_device_list(devs, false);
}

void report_error(const LibUsbConnection& connection) {
    if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        return;
    }
    std::lock_guard<std::mutex> lock(known_devices_mutex);
    known_devices.erase(connection.GetSessionId());
}
}  // end namespace libusb_inhouse_hotplug