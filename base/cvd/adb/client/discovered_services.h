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

#include <android-base/thread_annotations.h>

#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct IPv4Address {
    uint8_t bytes[4];

    bool operator==(const IPv4Address& other) const {
        return std::memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
    }
};

struct IPv6Address {
    uint8_t bytes[16];

    std::string to_string() const {
        std::string result;
        result.reserve(39);

        for (size_t i = 0; i < 16; i += 2) {
            uint16_t word = (static_cast<uint16_t>(bytes[i]) << 8) | bytes[i + 1];
            result += std::format("{:x}", word);

            if (i < 14) {
                result += ":";
            }
        }
        return result;
    }

    // Allow the struct to be used in an unordered_set
    bool operator<(const IPv6Address& other) const {
        return std::memcmp(bytes, other.bytes, sizeof(bytes)) < 0;
    }

    bool operator==(const IPv6Address& other) const {
        return std::memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
    }
};

struct IPv6AddressHash {
    std::size_t operator()(const IPv6Address& addr) const {
        return std::hash<std::string_view>{}(
                std::string_view(reinterpret_cast<const char*>(addr.bytes), sizeof(addr.bytes)));
    }
};

struct ServiceInfo {
    ServiceInfo() = default;

    ServiceInfo(const std::string& in_instance, const std::string& in_service,
                const std::string& hostname, const std::optional<IPv4Address>& in_v4_address,
                const std::unordered_set<IPv6Address, IPv6AddressHash>& in_v6_addresses,
                const uint16_t in_port, const std::vector<std::vector<uint8_t>>& txt)
        : instance(in_instance),
          service(in_service),
          hostname(hostname),
          v4_address(in_v4_address),
          v6_addresses(in_v6_addresses),
          port(in_port),
          attributes(ParseTxt(txt)) {}

    std::string instance;
    std::string service;
    std::string hostname;
    std::optional<IPv4Address> v4_address;
    std::unordered_set<IPv6Address, IPv6AddressHash> v6_addresses;
    uint16_t port;

    // Store keys/values from TXT resource record
    std::unordered_map<std::string, std::string> attributes;

    std::string v4_address_string() const {
        if (!v4_address.has_value()) {
            return "";
        }
        return std::format("{}.{}.{}.{}", v4_address->bytes[0], v4_address->bytes[1],
                           v4_address->bytes[2], v4_address->bytes[3]);
    }

    // Parse a key/value from a TXT record. Format expected is "key=value"
    static std::tuple<bool, std::string, std::string> ParseTxtKeyValue(const std::string& kv);
    static std::unordered_map<std::string, std::string> ParseTxt(
            const std::vector<std::vector<uint8_t>>& txt);
};

std::ostream& operator<<(std::ostream& os, const ServiceInfo& info);

enum ServiceInfoState {
    Created,
    Updated,
    Deleted,
};

namespace mdns {
class DiscoveredServices {
  public:
    void ServiceCreated(const ServiceInfo& service_info);

    // Return true if the provided service_info resulted in an update
    // of the internal state of DiscoveredServices
    bool ServiceUpdated(const ServiceInfo& service_info);

    void ServiceDeleted(const ServiceInfo& service_info);
    std::optional<ServiceInfo> FindInstance(const std::string& service,
                                            const std::string& instance);
    void ForEachServiceNamed(const std::string& service,
                             const std::function<void(const ServiceInfo&)>& callback);
    void ForAllServices(const std::function<void(const ServiceInfo&)>& callback);

  private:
    std::mutex services_mutex_;
    std::unordered_map<std::string, ServiceInfo> services_ GUARDED_BY(services_mutex_);
};

extern DiscoveredServices discovered_services;
}  // namespace mdns