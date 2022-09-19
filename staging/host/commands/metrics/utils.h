/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "host/commands/metrics/proto/cf_metrics_proto.h"

namespace metrics {
enum ClearcutServer : int {
  kLocal = 0,
  kStaging = 1,
  kProd = 2,
};

cuttlefish::MetricsEvent::OsType osType();
std::string osVersion();
std::string sessionId(uint64_t now);
std::string cfVersion();
std::string macAddress();
std::string company();
cuttlefish::MetricsEvent::VmmType vmmManager();
std::string vmmVersion();
uint64_t epochTimeMs();
std::string protoToStr(LogEvent* event);
cuttlefish::MetricsExitCodes postReq(std::string output, ClearcutServer server);
}  // namespace metrics
