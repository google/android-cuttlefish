/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "client/openscreen/mdns_service_info.h"

#include "adb_mdns.h"

using namespace openscreen;

namespace mdns {

std::string ServiceInfo::v4_address_string() const {
    std::stringstream ss;
    ss << v4_address;
    return ss.str();
}

std::string ServiceInfo::v6_address_string() const {
    std::stringstream ss;
    ss << v6_address;
    return ss.str();
}

// Parse a key/value from a TXT record. Format expected is "key=value"
std::tuple<bool, std::string, std::string> ParseTxtKeyValue(const std::string& kv) {
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

static std::unordered_map<std::string, std::string> ParseTxt(
        std::vector<std::vector<uint8_t>>& txt) {
    std::unordered_map<std::string, std::string> kv;
    for (auto& in_kv : txt) {
        std::string skv = std::string(in_kv.begin(), in_kv.end());
        auto [valid, key, value] = ParseTxtKeyValue(skv);
        if (!valid) {
            VLOG(MDNS) << "Bad TXT value '" << skv << "'";
            continue;
        }
        VLOG(MDNS) << "Parsed TXT key='" << key << "', value='" << value << "'";
        kv[key] = value;
    }
    return kv;
}

ErrorOr<ServiceInfo> DnsSdInstanceEndpointToServiceInfo(
        const discovery::DnsSdInstanceEndpoint& endpoint) {
    ServiceInfo service_info;

    service_info.instance = endpoint.instance_id();
    service_info.service = endpoint.service_id();
    service_info.port = endpoint.port();
    for (const IPAddress& address : endpoint.addresses()) {
        if (!service_info.v4_address && address.IsV4()) {
            service_info.v4_address = address;
        } else if (!service_info.v6_address && address.IsV6()) {
            service_info.v6_address = address;
        }
    }
    CHECK(service_info.v4_address || service_info.v6_address);

    auto txt = endpoint.txt().GetData();
    service_info.attributes = ParseTxt(txt);

    return service_info;
}

}  // namespace mdns
