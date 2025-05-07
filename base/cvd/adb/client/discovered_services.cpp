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

void DiscoveredServices::ServiceUpdated(const ServiceInfo& service_info) {
    std::lock_guard lock(services_mutex_);
    VLOG(MDNS) << "Service update " << service_info;
    services_[fq_name(service_info)] = service_info;
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
