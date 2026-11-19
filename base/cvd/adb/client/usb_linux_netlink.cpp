/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "usb_linux_netlink.h"

#include "adb_trace.h"

NetlinkMessage::NetlinkMessage(const char* buffer, ssize_t len) {
    VLOG(USB) << "\nNetlink: NEW MESSAGE";
    if (len <= 0) {
        return;
    }

    const char* current_ptr = buffer;
    const char* const end_ptr = buffer + len;

    while (current_ptr < end_ptr) {
        std::string_view entry(current_ptr);

        current_ptr += entry.size() + 1;

        // Skip if no '=' delimiter is found
        size_t sep = entry.find('=');
        if (sep == std::string_view::npos) {
            continue;
        }

        std::string key(entry.substr(0, sep));
        std::string value(entry.substr(sep + 1));

        if (key.empty()) {
            continue;
        }

        VLOG(USB) << "Netlink: '" << key << "' = '" << value << "'";
        attrs_[std::move(key)] = std::move(value);
    }
}

std::string NetlinkMessage::attr(const std::string& key) const {
    auto it = attrs_.find(key);
    return it != attrs_.end() ? it->second : "";
}

bool NetlinkMessage::has_attr(const std::string& key, std::string_view value) const {
    auto it = attrs_.find(key);
    return it != attrs_.end() && it->second == value;
}