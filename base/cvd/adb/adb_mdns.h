/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <functional>
#include <optional>
#include <string>
#include <string_view>

// The rules for Service Names [RFC6335] state that they may be no more
// than fifteen characters long (not counting the mandatory underscore),
// consisting of only letters, digits, and hyphens, must begin and end
// with a letter or digit, must not contain consecutive hyphens, and
// must contain at least one letter.
#define ADB_MDNS_SERVICE_TYPE "adb"
#define ADB_MDNS_TLS_PAIRING_TYPE "adb-tls-pairing"
#define ADB_MDNS_TLS_CONNECT_TYPE "adb-tls-connect"
#define ADB_FULL_MDNS_SERVICE_TYPE(atype) ("_" atype "._tcp")

#define ADB_SERVICE_TCP ADB_FULL_MDNS_SERVICE_TYPE(ADB_MDNS_SERVICE_TYPE)
#define ADB_SERVICE_TLS ADB_FULL_MDNS_SERVICE_TYPE(ADB_MDNS_TLS_CONNECT_TYPE)
#define ADB_SERVICE_PAIR ADB_FULL_MDNS_SERVICE_TYPE(ADB_MDNS_TLS_PAIRING_TYPE)

// Client/service versions are initially defined to be matching,
// but may go out of sync as different clients and services
// try to talk to each other.
#define ADB_SECURE_SERVICE_VERSION 1
#define ADB_SECURE_CLIENT_VERSION ADB_SECURE_SERVICE_VERSION

constexpr int kADBTransportServiceRefIndex = 0;
constexpr int kADBSecurePairingServiceRefIndex = 1;
constexpr int kADBSecureConnectServiceRefIndex = 2;
constexpr int kNumADBDNSServices = 3;

extern const char* _Nonnull kADBDNSServices[kNumADBDNSServices];
extern const char* _Nonnull kADBDNSServiceTxtRecords[kNumADBDNSServices];

#if ADB_HOST
#include "client/openscreen/mdns_service_info.h"
// ADB Secure DNS service interface. Used to query what ADB Secure DNS services have been
// resolved, and to run some kind of callback for each one.
using adb_secure_foreach_service_callback = std::function<void(const mdns::ServiceInfo& si)>;

// Tries to connect to a |service_name| if found. Returns true if found and
// connected, false otherwise.
bool adb_secure_connect_by_service_name(const std::string& instance_name);

// Returns the index in kADBDNSServices array if |reg_type| matches a service name, otherwise
// std::nullopt.
std::optional<int> adb_DNSServiceIndexByName(std::string_view reg_type);
// Returns true if auto-connect is allowed for |service_name| and |instance_name|.
// See ADB_MDNS_AUTO_CONNECT environment variable for more info.
bool adb_DNSServiceShouldAutoConnect(std::string_view service_name, std::string_view instance_name);

std::string mdns_check();
std::string mdns_list_discovered_services();

std::optional<mdns::ServiceInfo> mdns_get_connect_service_info(const std::string& name);
std::optional<mdns::ServiceInfo> mdns_get_pairing_service_info(const std::string& name);

// Return the location where adb host stores paired devices
std::string get_user_known_hosts_path();
#endif  // ADB_HOST
