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

#include <optional>
#include <string>

#include "discovered_services.h"

// Whatever mdns engine is used, this is the sole entry point into ADB.
void OnServiceReceiverResult(const ServiceInfo& info, ServiceInfoState state);

std::optional<ServiceInfo> mdns_get_connect_service_info(const std::string& name);
std::optional<ServiceInfo> mdns_get_pairing_service_info(const std::string& name);
