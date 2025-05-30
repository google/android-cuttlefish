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

#include "client/openscreen/mdns_service_info.h"

namespace mdns {
class DiscoveredServices {
  public:
    void ServiceCreated(const ServiceInfo& service_info);
    void ServiceUpdated(const ServiceInfo& service_info);
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