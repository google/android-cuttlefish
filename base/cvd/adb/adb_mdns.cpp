/*
 * Copyright (C) 2020 Android Open Source Project
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

#define TRACE_TAG TRANSPORT

#include "adb_mdns.h"

#include <algorithm>
#include <set>

#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "adb_trace.h"

const char* kADBDNSServices[] = {ADB_SERVICE_TCP, ADB_SERVICE_PAIR, ADB_SERVICE_TLS};

#if ADB_HOST
namespace {

std::atomic<bool> g_allowedlist_configured{false};
[[clang::no_destroy]] std::set<int> g_autoconn_allowedlist;

void config_auto_connect_services() {
    bool expected = false;
    if (!g_allowedlist_configured.compare_exchange_strong(expected, true)) {
        return;
    }

    // ADB_MDNS_AUTO_CONNECT is a comma-delimited list of mdns services
    // that are allowed to auto-connect. By default, only allow "adb-tls-connect"
    // to auto-connect, since this is filtered down to auto-connect only to paired
    // devices.
    g_autoconn_allowedlist.insert(kADBSecureConnectServiceRefIndex);
    const char* srvs = getenv("ADB_MDNS_AUTO_CONNECT");
    if (!srvs) {
        return;
    }

    if (strcmp(srvs, "0") == 0) {
        D("Disabling all auto-connecting");
        g_autoconn_allowedlist.clear();
        return;
    }

    if (strcmp(srvs, "all") == 0) {
        D("Allow all auto-connecting");
        g_autoconn_allowedlist.insert(kADBTransportServiceRefIndex);
        return;
    }

    // Selectively choose which services to allow auto-connect.
    // E.g. ADB_MDNS_AUTO_CONNECT=adb,adb-tls-connect would allow
    // _adb._tcp and _adb-tls-connnect._tcp services to auto-connect.
    auto srvs_list = android::base::Split(srvs, ",");
    std::set<int> new_allowedlist;
    for (const auto& item : srvs_list) {
        auto full_srv = android::base::StringPrintf("_%s._tcp", item.data());
        std::optional<int> idx = adb_DNSServiceIndexByName(full_srv);
        if (idx.has_value()) {
            new_allowedlist.insert(*idx);
        }
    }

    if (!new_allowedlist.empty()) {
        g_autoconn_allowedlist = std::move(new_allowedlist);
    }

    std::string res;
    std::for_each(g_autoconn_allowedlist.begin(), g_autoconn_allowedlist.end(), [&](const int& i) {
        res += kADBDNSServices[i];
        res += ",";
    });
    D("mdns auto-connect allowedlist: [%s]", res.data());
}

}  // namespace

std::optional<int> adb_DNSServiceIndexByName(std::string_view reg_type) {
    for (int i = 0; i < kNumADBDNSServices; ++i) {
        if (!strncmp(reg_type.data(), kADBDNSServices[i], strlen(kADBDNSServices[i]))) {
            return i;
        }
    }
    return std::nullopt;
}

bool adb_DNSServiceShouldAutoConnect(std::string_view reg_type, std::string_view service_name) {
    config_auto_connect_services();

    // Try to auto-connect to any "_adb" or "_adb-tls-connect" services excluding emulator services.
    std::optional<int> index = adb_DNSServiceIndexByName(reg_type);
    if (!index ||
        (index != kADBTransportServiceRefIndex && index != kADBSecureConnectServiceRefIndex)) {
        return false;
    }
    if (!g_autoconn_allowedlist.contains(*index)) {
        D("Auto-connect for reg_type '%s' disabled", reg_type.data());
        return false;
    }
    // Ignore adb-EMULATOR* service names, as it interferes with the
    // emulator ports that are already connected.
    if (android::base::StartsWith(service_name, "adb-EMULATOR")) {
        LOG(INFO) << "Ignoring emulator transport service [" << service_name << "]";
        return false;
    }
    return true;
}

#endif  // ADB_HOST
