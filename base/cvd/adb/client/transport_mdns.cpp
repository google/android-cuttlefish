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

#define TRACE_TAG MDNS

#include "transport.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "adb_client.h"
#include "adb_mdns.h"
#include "adb_trace.h"
#include "adb_utils.h"
#include "adb_wifi.h"
#include "client/discovered_services.h"
#include "client/mdns_utils.h"
#include "fdevent/fdevent.h"
#include "mdns_tracker.h"
#include "sysdeps.h"

namespace {

static void RequestConnectToDevice(const ServiceInfo& info) {
    // Connecting to a device does not happen often. We spawn a new thread each time.
    // Let's re-evaluate if we need a thread-pool or a background thread if this ever becomes
    // a perf bottleneck.
    std::thread([=] {
        VLOG(MDNS) << "Attempting to secure connect to instance '" << info << "'";
        std::string response;
        connect_device(std::format("{}.{}", info.instance, info.service), &response);
        VLOG(MDNS) << std::format("secure connect to {} regtype {} ({}:{}) : {}", info.instance,
                                  info.service, info.v4_address_string(), info.port, response);
    }).detach();
}

void AttemptAutoConnect(const std::reference_wrapper<const ServiceInfo> info) {
    if (!adb_DNSServiceShouldAutoConnect(info.get().service, info.get().instance)) {
        VLOG(MDNS) << "Don't auto-connect to " << info.get().instance << "." << info.get().service;
        return;
    }

    if (!info.get().v4_address.has_value()) {
        return;
    }

    const auto index = adb_DNSServiceIndexByName(info.get().service);
    if (!index) {
        VLOG(MDNS) << "Unknown service" << info.get().service;
        return;
    }

    // Don't try to auto-connect if not in the keystore.
    if (*index == kADBSecureConnectServiceRefIndex &&
        !known_wifi_hosts_file.IsKnownHost(info.get().instance)) {
        VLOG(MDNS) << "instance_name=" << info.get().instance << " not in keystore";
        return;
    }

    RequestConnectToDevice(info.get());
}

bool ConnectAdbSecureDevice(const ServiceInfo& info) {
    if (!known_wifi_hosts_file.IsKnownHost(info.instance)) {
        VLOG(MDNS) << "serviceName=" << info.instance << " not in keystore";
        return false;
    }

    RequestConnectToDevice(info);
    return true;
}

}  // namespace

// Callback provided to service receiver for updates.
void OnServiceReceiverResult(const ServiceInfo& info, ServiceInfoState state) {
    bool updated = true;
    switch (state) {
        case Created: {
            VLOG(MDNS) << "mdns stack reported service created " << info;
            mdns::discovered_services.ServiceCreated(info);
            AttemptAutoConnect(info);
            break;
        }
        case Updated: {
            VLOG(MDNS) << "mdns stack reported service updated " << info;
            updated = mdns::discovered_services.ServiceUpdated(info);
            if (updated) {
                AttemptAutoConnect(info);
            }
            break;
        }
        case Deleted: {
            VLOG(MDNS) << "mdns stack reported service deleted " << info;
            mdns::discovered_services.ServiceDeleted(info);
            break;
        }
    }

    if (updated) {
        update_mdns_trackers();
    }
}

/////////////////////////////////////////////////////////////////////////////////

void init_mdns_transport_discovery() {
    VLOG(MDNS) << "mdns discovery is disabled in this build";
}

bool adb_secure_connect_by_service_name(const std::string& instance_name) {
    auto info = mdns::discovered_services.FindInstance(ADB_SERVICE_TLS, instance_name);
    if (info.has_value()) {
        return ConnectAdbSecureDevice(*info);
    }
    return false;
}

std::string mdns_check() {
    return "ERROR: mdns discovery disabled";
}

std::string mdns_list_discovered_services() {
    std::string result;
    auto cb = [&](const ServiceInfo& si) {
        result += std::format("{}\t{}\t{}:{}\n", si.instance, si.service, si.v4_address_string(),
                              si.port);
    };
    mdns::discovered_services.ForAllServices(cb);
    return result;
}

std::optional<ServiceInfo> mdns_get_connect_service_info(const std::string& name) {
    CHECK(!name.empty());

    auto mdns_instance = mdns::mdns_parse_instance_name(name);
    if (!mdns_instance.has_value()) {
        D("Failed to parse mDNS name [%s]", name.data());
        return std::nullopt;
    }

    std::string fq_service =
            std::format("{}.{}", mdns_instance->service_name, mdns_instance->transport_type);
    return mdns::discovered_services.FindInstance(fq_service, mdns_instance->instance_name);
}

std::optional<ServiceInfo> mdns_get_pairing_service_info(const std::string& name) {
    CHECK(!name.empty());

    auto mdns_instance = mdns::mdns_parse_instance_name(name);
    if (!mdns_instance.has_value()) {
        D("Failed to parse mDNS name [%s]", name.data());
        return {};
    }

    return mdns::discovered_services.FindInstance(ADB_SERVICE_PAIR, mdns_instance->instance_name);
}
