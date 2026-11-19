/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <string>

#include "adb.h"
#include "adb_known_hosts.pb.h"

void adb_wifi_pair_device(const std::string& host, const std::string& password,
                          std::string& response);

// An accessor to the list of known hosts (known_host). Nothing is cached, every operation
// hits the disk.
// TODO: Convert this to a write-through cache.
class KnownWifiHostsFile {
  public:
    KnownWifiHostsFile();

    // For testing, we allow a custom known_host location
    explicit KnownWifiHostsFile(const std::string& keystore_path) : keystore_path_(keystore_path) {}

    // Location of the known_host file
    std::string KeyStorePath() const { return keystore_path_; }

    // Add host to the known_host file
    bool AddKnownHost(const std::string& host);

    // Load known_host file and return true if that host is considered paired.
    bool IsKnownHost(const std::string& host);

    // Delete known_host file
    void Clear();

  private:
    std::string keystore_path_;
    adb::proto::AdbKnownHosts Load();
};

extern KnownWifiHostsFile known_wifi_hosts_file;
