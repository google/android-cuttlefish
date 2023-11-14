/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <string.h>

#include <clientanalytics.pb.h>
#include "host/commands/metrics/metrics_defs.h"

namespace metrics {
enum ClearcutServer : int {
  kLocal = 0,
  kStaging = 1,
  kProd = 2,
};

std::string GetOsName();
std::string GetOsVersion();
std::string GenerateSessionId(uint64_t now);
std::string GetCfVersion();
std::string GetMacAddress();
std::string GetCompany();
std::string GetVmmVersion();
uint64_t GetEpochTimeMs();
std::string ProtoToString(LogEvent* event);
cuttlefish::MetricsExitCodes PostRequest(const std::string& output,
                                         ClearcutServer server);

}  // namespace metrics
