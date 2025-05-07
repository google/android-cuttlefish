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

#include "usb_libusb_hotplug.h"

#include "adb_trace.h"
#include "adb_utils.h"
#include "sysdeps.h"
#include "usb_libusb.h"
#include "usb_libusb_inhouse_hotplug.h"

#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#endif

#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#include "libusb/libusb.h"

using namespace std::chrono_literals;

// Keep track of connected devices so we can notify the transport system of
// when we are done scanning USB devices.
static std::atomic<int> connecting_devices(0);

// We usually detect disconnection when a device Read() operation fails. However, when a device
// is detached, the Read thread is not running so unplugging does not result in a Read failure.
// In order to let the transport system know that a detached device is disconnected, we keep track
// of the connections we created.
static std::mutex connections_mutex_ [[clang::no_destroy]];
static std::unordered_map<libusb_device*, std::weak_ptr<LibUsbConnection>> GUARDED_BY(
        connections_mutex_) connections_ [[clang::no_destroy]];

static void process_device(libusb_device* raw_device) {
    auto device = std::make_unique<LibUsbDevice>(raw_device);
    if (!device) {
        LOG(FATAL) << "Failed to construct LibusbConnection";
    }

    if (!device->IsInitialized()) {
        VLOG(USB) << std::format("Can't init address='{}', serial='{}'", device->GetAddress(),
                                 device->GetSerial());
        return;
    }

    if (!transport_server_owns_device(device->GetAddress(), device->GetSerial())) {
        VLOG(USB) << "ignoring device " << device->GetSerial() << ": this server owns '"
                  << transport_get_one_device() << "'";
        return;
    }

    VLOG(USB) << "constructed LibusbConnection for device " << device->GetSerial();

    auto address = device->GetAddress();
    auto serial = device->GetSerial();
    auto connection = std::make_shared<LibUsbConnection>(std::move(device));
    connection->Init();

    // Keep track of connection so we can call Close on it upon disconnection
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.emplace(libusb_ref_device(raw_device), connection);
    }
    register_libusb_transport(connection, serial.c_str(), address.c_str(), true);
}

static void device_disconnected(libusb_device* dev) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(dev);
    if (it != connections_.end()) {
        // We need to ensure that we don't destroy the LibusbConnection on this thread,
        // as we're in a context with internal libusb mutexes held.
        libusb_device* device = it->first;
        std::weak_ptr<LibUsbConnection> connection_weak = it->second;
        connections_.erase(it);
        fdevent_run_on_looper([connection_weak]() {
            auto connection = connection_weak.lock();
            if (connection) {
                connection->Stop();
                VLOG(USB) << "libusb_hotplug: device disconnected: (Stop requested)";
                if (connection->IsDetached() && connection->transport_ != nullptr) {
                    connection->OnError("Detached device has disconnected");
                }
            } else {
                VLOG(USB) << "libusb_hotplug: device disconnected: (Already destroyed)";
            }
        });
        libusb_unref_device(device);
    }
}

static void device_connected(libusb_device* device) {
#if defined(__linux__)
    // Android's host linux libusb uses netlink instead of udev for device hotplug notification,
    // which means we can get hotplug notifications before udev has updated ownership/perms on the
    // device. Since we're not going to be able to link against the system's libudev any time soon,
    // poll for accessibility changes with inotify until a timeout expires.
    libusb_ref_device(device);
    auto thread = std::thread([device]() {
        std::string bus_path =
                android::base::StringPrintf("/dev/bus/usb/%03d/", libusb_get_bus_number(device));
        std::string device_path = android::base::StringPrintf("%s/%03d", bus_path.c_str(),
                                                              libusb_get_device_address(device));
        auto deadline = std::chrono::steady_clock::now() + 1s;
        unique_fd infd(inotify_init1(IN_CLOEXEC | IN_NONBLOCK));
        if (infd == -1) {
            PLOG(FATAL) << "failed to create inotify fd";
        }

        // Register the watch first, and then check for accessibility, to avoid a race.
        // We can't watch the device file itself, as that requires us to be able to access it.
        if (inotify_add_watch(infd.get(), bus_path.c_str(), IN_ATTRIB) == -1) {
            PLOG(ERROR) << "failed to register inotify watch on '" << bus_path
                        << "', falling back to sleep";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            adb_pollfd pfd = {.fd = infd.get(), .events = POLLIN, .revents = 0};

            while (access(device_path.c_str(), R_OK | W_OK) == -1) {
                auto timeout = deadline - std::chrono::steady_clock::now();
                if (timeout < 0s) {
                    break;
                }

                uint64_t ms = timeout / 1ms;
                int rc = adb_poll(&pfd, 1, ms);
                if (rc == -1) {
                    if (errno == EINTR) {
                        continue;
                    } else {
                        LOG(WARNING) << "timeout expired while waiting for device accessibility";
                        break;
                    }
                }

                union {
                    struct inotify_event ev;
                    char bytes[sizeof(struct inotify_event) + NAME_MAX + 1];
                } buf;

                rc = adb_read(infd.get(), &buf, sizeof(buf));
                if (rc == -1) {
                    break;
                }

                // We don't actually care about the data: we might get spurious events for
                // other devices on the bus, but we'll double check in the loop condition.
                continue;
            }
        }

        process_device(device);
        if (--connecting_devices == 0) {
            adb_notify_device_scan_complete();
        }
        libusb_unref_device(device);
    });
    thread.detach();
#else
    process_device(device);
#endif
}

static auto& hotplug_queue = *new BlockingQueue<std::pair<libusb_hotplug_event, libusb_device*>>();
static void hotplug_thread() {
    VLOG(USB) << "libusb hotplug thread started";
    adb_thread_setname("libusb hotplug");
    while (true) {
        hotplug_queue.PopAll([](std::pair<libusb_hotplug_event, libusb_device*> pair) {
            libusb_hotplug_event event = pair.first;
            libusb_device* device = pair.second;
            if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
                VLOG(USB) << "libusb hotplug: device arrived";
                device_connected(device);
            } else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
                VLOG(USB) << "libusb hotplug: device left";
                device_disconnected(device);
            } else {
                LOG(WARNING) << "unknown libusb hotplug event: " << event;
            }
        });
    }
}

LIBUSB_CALL int hotplug_callback(libusb_context*, libusb_device* device, libusb_hotplug_event event,
                                 void*) {
    // We're called with the libusb lock taken. Call these on a separate thread outside of this
    // function so that the usb_handle mutex is always taken before the libusb mutex.
    static std::once_flag once;
    std::call_once(once, []() { std::thread(hotplug_thread).detach(); });

    if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
        ++connecting_devices;
    }
    hotplug_queue.Push({event, device});
    return 0;
}

namespace libusb {

static void usb_init_libusb_hotplug() {
    int rc = libusb_hotplug_register_callback(
            nullptr,
            static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                                              LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
            LIBUSB_HOTPLUG_ENUMERATE, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
            LIBUSB_CLASS_PER_INTERFACE, hotplug_callback, nullptr, nullptr);

    if (rc != LIBUSB_SUCCESS) {
        LOG(FATAL) << "failed to register libusb hotplug callback";
    }

    // Spawn a thread for libusb_handle_events.
    std::thread([]() {
        adb_thread_setname("libusb");
        while (true) {
            libusb_handle_events(nullptr);
        }
    }).detach();
}

static void usb_init_inhouse_hotplug() {
    // Spawn a thread for handling USB events
    std::thread([]() {
        adb_thread_setname("libusb_inhouse_hotplug");
        struct timeval timeout{(time_t)libusb_inhouse_hotplug::kScan_rate_s.count(), 0};
        while (true) {
            VLOG(USB) << "libusb thread iteration";
            libusb_handle_events_timeout_completed(nullptr, &timeout, nullptr);
            libusb_inhouse_hotplug::scan();
        }
    }).detach();
}

void usb_init() {
    VLOG(USB) << "initializing libusb...";
    int rc = libusb_init(nullptr);
    if (rc != 0) {
        LOG(WARNING) << "failed to initialize libusb: " << libusb_error_name(rc);
        return;
    }

    if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        usb_init_libusb_hotplug();
    } else {
        usb_init_inhouse_hotplug();
    }
}
}  // namespace libusb