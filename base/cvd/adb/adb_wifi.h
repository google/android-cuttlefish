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

#include <optional>
#include <string>

#include "adb.h"

#if ADB_HOST

void adb_wifi_pair_device(const std::string& host, const std::string& password,
                          std::string& response);
bool adb_wifi_is_known_host(const std::string& host);

#else  // !ADB_HOST

struct AdbdAuthContext;

void adbd_wifi_init(AdbdAuthContext* ctx);
void adbd_wifi_secure_connect(atransport* t);
void adbd_send_tls_server_port(uint16_t port);

#endif
