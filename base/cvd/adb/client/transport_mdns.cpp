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

#include <discovery/common/config.h>
#include <discovery/common/reporting_client.h>
#include <discovery/public/dns_sd_service_factory.h>
#include <discovery/public/dns_sd_service_watcher.h>
#include <platform/api/network_interface.h>
#include <platform/api/serial_delete_ptr.h>
#include <platform/base/error.h>
#include <platform/base/interface_info.h>

#include "adb_client.h"
#include "adb_mdns.h"
#include "adb_trace.h"
#include "adb_utils.h"
#include "adb_wifi.h"
#include "client/discovered_services.h"
#include "client/mdns_utils.h"
#include "client/openscreen/platform/task_runner.h"
#include "fdevent/fdevent.h"
#include "mdns_tracker.h"
#include "sysdeps.h"

namespace {

using namespace mdns;
using namespace openscreen;
using ServiceWatcher = discovery::DnsSdServiceWatcher<ServiceInfo>;
using ServicesUpdatedState = ServiceWatcher::ServicesUpdatedState;

struct DiscoveryState;
DiscoveryState* g_state = nullptr;

class DiscoveryReportingClient : public discovery::ReportingClient {
  public:
    void OnFatalError(Error error) override {
        LOG(ERROR) << "Encountered fatal discovery error: " << error;
        got_fatal_ = true;
    }

    void OnRecoverableError(Error error) override {
        LOG(ERROR) << "Encountered recoverable discovery error: " << error;
    }

    bool GotFatalError() const { return got_fatal_; }

  private:
    std::atomic<bool> got_fatal_{false};
};

struct DiscoveryState {
    std::optional<discovery::Config> config;
    SerialDeletePtr<discovery::DnsSdService> service;
    std::unique_ptr<DiscoveryReportingClient> reporting_client;
    std::unique_ptr<AdbOspTaskRunner> task_runner;
    std::vector<std::unique_ptr<ServiceWatcher>> watchers;
    InterfaceInfo interface_info;
};

static void RequestConnectToDevice(const ServiceInfo& info) {
    // Connecting to a device does not happen often. We spawn a new thread each time.
    // Let's re-evaluate if we need a thread-pool or a background thread if this ever becomes
    // a perf bottleneck.
    std::thread([=] {
        VLOG(MDNS) << "Attempting to secure connect to instance=" << info.instance
                   << " service=" << info.service << " addr4=%s" << info.v4_address << ":"
                   << info.port;
        std::string response;
        connect_device(std::format("{}.{}", info.instance, info.service), &response);
        VLOG(MDNS) << std::format("secure connect to {} regtype {} ({}:{}) : {}", info.instance,
                                  info.service, info.v4_address_string(), info.port, response);
    }).detach();
}

// Callback provided to service receiver for updates.
void OnServiceReceiverResult(std::vector<std::reference_wrapper<const ServiceInfo>>,
                             std::reference_wrapper<const ServiceInfo> info,
                             ServicesUpdatedState state) {
    switch (state) {
        case ServicesUpdatedState::EndpointCreated: {
            discovered_services.ServiceCreated(info);
            break;
        }
        case ServicesUpdatedState::EndpointUpdated: {
            discovered_services.ServiceUpdated(info);
            break;
        }
        case ServicesUpdatedState::EndpointDeleted: {
            discovered_services.ServiceDeleted(info);
            break;
        }
    }

    update_mdns_trackers();

    switch (state) {
        case ServicesUpdatedState::EndpointCreated:
        case ServicesUpdatedState::EndpointUpdated:
            if (adb_DNSServiceShouldAutoConnect(info.get().service, info.get().instance) &&
                info.get().v4_address) {
                auto index = adb_DNSServiceIndexByName(info.get().service);
                if (!index) {
                    return;
                }

                // Don't try to auto-connect if not in the keystore.
                if (*index == kADBSecureConnectServiceRefIndex &&
                    !adb_wifi_is_known_host(info.get().instance)) {
                    VLOG(MDNS) << "instance_name=" << info.get().instance << " not in keystore";
                    return;
                }

                RequestConnectToDevice(info.get());
            }
            break;
        default:
            break;
    }
}

std::optional<discovery::Config> GetConfigForAllInterfaces() {
    auto interface_infos = GetNetworkInterfaces();

    discovery::Config config;

    // The host only consumes mDNS traffic. It doesn't publish anything.
    // Avoid creating an mDNSResponder that will listen with authority
    // to answer over no domain.
    config.enable_publication = false;

    for (const auto& interface : interface_infos) {
        if (interface.GetIpAddressV4() || interface.GetIpAddressV6()) {
            config.network_info.push_back({interface});
            VLOG(MDNS) << "Listening on interface [" << interface << "]";
        }
    }

    if (config.network_info.empty()) {
        VLOG(MDNS) << "No available network interfaces for mDNS discovery";
        return std::nullopt;
    }

    return config;
}

void StartDiscovery() {
    CHECK(!g_state);
    g_state = new DiscoveryState();
    g_state->task_runner = std::make_unique<AdbOspTaskRunner>();
    g_state->reporting_client = std::make_unique<DiscoveryReportingClient>();

    g_state->task_runner->PostTask([]() {
        g_state->config = GetConfigForAllInterfaces();
        if (!g_state->config) {
            VLOG(MDNS) << "No mDNS config. Aborting StartDiscovery()";
            return;
        }

        VLOG(MDNS) << "Starting discovery on " << (*g_state->config).network_info.size()
                   << " interfaces";

        g_state->service = discovery::CreateDnsSdService(
                g_state->task_runner.get(), g_state->reporting_client.get(), *g_state->config);
        // Register a receiver for each service type
        for (int i = 0; i < kNumADBDNSServices; ++i) {
            auto watcher = std::make_unique<ServiceWatcher>(
                    g_state->service.get(), kADBDNSServices[i], DnsSdInstanceEndpointToServiceInfo,
                    OnServiceReceiverResult);
            watcher->StartDiscovery();
            g_state->watchers.push_back(std::move(watcher));

            if (g_state->reporting_client->GotFatalError()) {
                for (auto& w : g_state->watchers) {
                    if (w->is_running()) {
                        w->StopDiscovery();
                    }
                }
                break;
            }
        }
    });
}

bool ConnectAdbSecureDevice(const ServiceInfo& info) {
    if (!adb_wifi_is_known_host(info.instance)) {
        VLOG(MDNS) << "serviceName=" << info.instance << " not in keystore";
        return false;
    }

    RequestConnectToDevice(info);
    return true;
}

}  // namespace

/////////////////////////////////////////////////////////////////////////////////

void init_mdns_transport_discovery() {
    const char* mdns_osp = getenv("ADB_MDNS_OPENSCREEN");
    if (mdns_osp && strcmp(mdns_osp, "0") == 0) {
        LOG(WARNING) << "Environment variable ADB_MDNS_OPENSCREEN disregarded";
    } else {
        VLOG(MDNS) << "Openscreen mdns discovery enabled";
        StartDiscovery();
    }
}

bool adb_secure_connect_by_service_name(const std::string& instance_name) {
    if (!g_state || g_state->watchers.empty()) {
        VLOG(MDNS) << "Mdns not enabled";
        return false;
    }

    auto info = discovered_services.FindInstance(ADB_SERVICE_TLS, instance_name);
    if (info.has_value()) {
        return ConnectAdbSecureDevice(*info);
    }
    return false;
}

std::string mdns_check() {
    if (!g_state) {
        return "ERROR: mdns discovery disabled";
    }

    return "mdns daemon version [Openscreen discovery 0.0.0]";
}

std::string mdns_list_discovered_services() {
    if (!g_state || g_state->watchers.empty()) {
        return "";
    }

    std::string result;
    auto cb = [&](const mdns::ServiceInfo& si) {
        result += std::format("{}\t{}\t{}:{}\n", si.instance, si.service, si.v4_address_string(),
                              si.port);
    };
    discovered_services.ForAllServices(cb);
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
    return discovered_services.FindInstance(fq_service, mdns_instance->instance_name);
}

std::optional<ServiceInfo> mdns_get_pairing_service_info(const std::string& name) {
    CHECK(!name.empty());

    auto mdns_instance = mdns::mdns_parse_instance_name(name);
    if (!mdns_instance.has_value()) {
        D("Failed to parse mDNS name [%s]", name.data());
        return {};
    }

    return discovered_services.FindInstance(ADB_SERVICE_PAIR, mdns_instance->instance_name);
}
