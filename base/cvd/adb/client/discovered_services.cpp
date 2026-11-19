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

#include "discovered_services.h"

#include "adb_trace.h"

#include <algorithm>

std::ostream& operator<<(std::ostream& os, const ServiceInfo& info) {
    os << info.instance << "." << info.service << ":" << info.port << "";
    return os;
}

// Parse a key/value from a TXT record. Format expected is "key=value"
std::tuple<bool, std::string, std::string> ServiceInfo::ParseTxtKeyValue(const std::string& kv) {
    auto split_loc = std::ranges::find(kv, static_cast<uint8_t>('='));
    if (split_loc == kv.end()) {
        return {false, "", ""};
    }
    std::string key;
    std::string value;

    key.assign(kv.begin(), split_loc);
    if (split_loc + 1 != kv.end()) {
        value.assign(split_loc + 1, kv.end());
    }

    if (key.empty()) {
        return {false, key, value};
    }
    return {true, key, value};
}

std::unordered_map<std::string, std::string> ServiceInfo::ParseTxt(
        const std::vector<std::vector<uint8_t>>& txt) {
    std::unordered_map<std::string, std::string> kv;
    for (auto& in_kv : txt) {
        std::string skv = std::string(in_kv.begin(), in_kv.end());
        auto [valid, key, value] = ParseTxtKeyValue(skv);
        if (!valid) {
            VLOG(MDNS) << "Bad TXT value '" << skv << "'";
            continue;
        }
        kv[key] = value;
    }
    return kv;
}

namespace mdns {
DiscoveredServices discovered_services [[clang::no_destroy]];

static std::string fq_name(const ServiceInfo& si) {
    return std::format("{}.{}", si.instance, si.service);
}

void DiscoveredServices::ServiceCreated(const ServiceInfo& service_info) {
    std::lock_guard lock(services_mutex_);
    VLOG(MDNS) << "Service created " << service_info;
    services_[fq_name(service_info)] = service_info;
}

bool DiscoveredServices::ServiceUpdated(const ServiceInfo& service_info) {
    std::lock_guard lock(services_mutex_);

    const auto key = fq_name(service_info);
    if (!services_.contains(key)) {
        services_[key] = service_info;
        return true;
    }

    auto& current_service = services_[key];
    bool updated = false;

    if (service_info.v4_address.has_value() &&
        service_info.v4_address != current_service.v4_address) {
        current_service.v4_address = service_info.v4_address;
        updated = true;
    }

    for (auto& new_address : service_info.v6_addresses) {
        if (!current_service.v6_addresses.contains(new_address)) {
            updated = true;
            current_service.v6_addresses.insert(new_address);
        }
    }

    if (service_info.port != current_service.port) {
        current_service.port = service_info.port;
        updated = true;
    }

    if (service_info.attributes != current_service.attributes) {
        current_service.attributes = service_info.attributes;
        updated = true;
    }

    if (updated) {
        VLOG(MDNS) << "Service update " << service_info;
    }

    return updated;
}

void DiscoveredServices::ServiceDeleted(const ServiceInfo& service_info) {
    std::lock_guard lock(services_mutex_);
    VLOG(MDNS) << "Service deleted " << service_info;
    services_.erase(fq_name(service_info));
}

std::optional<ServiceInfo> DiscoveredServices::FindInstance(const std::string& service,
                                                            const std::string& instance) {
    std::lock_guard lock(services_mutex_);
    std::string fully_qualified_name = std::format("{}.{}", instance, service);
    if (!services_.contains(fully_qualified_name)) {
        return {};
    }
    return services_[fully_qualified_name];
}

void DiscoveredServices::ForEachServiceNamed(
        const std::string& service_name, const std::function<void(const ServiceInfo&)>& callback) {
    std::lock_guard lock(services_mutex_);
    for (const auto& [_, value] : services_) {
        if (value.service != service_name) {
            continue;
        }
        callback(value);
    }
}
void DiscoveredServices::ForAllServices(const std::function<void(const ServiceInfo&)>& callback) {
    std::lock_guard lock(services_mutex_);
    for (const auto& [_, value] : services_) {
        callback(value);
    }
}
}  // namespace mdns
