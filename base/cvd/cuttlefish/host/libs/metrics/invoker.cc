/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/metrics/invoker.h"

#include <optional>
#include <string>
#include <string_view>

#include "cuttlefish/common/libs/utils/environment.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kAcloud = "acloud";
constexpr std::string_view kHostOrchestrator = "host_orchestrator";
constexpr std::string_view kInvokerVariable = "CVD_INVOKER";

}  // namespace

Invoker GetInvoker() {
  const std::optional<std::string> invoker_value =
      StringFromEnv(std::string(kInvokerVariable));
  if (!invoker_value) {
    return Invoker::None;
  } else if (invoker_value == kAcloud) {
    return Invoker::Acloud;
  } else if (invoker_value == kHostOrchestrator) {
    return Invoker::HostOrchestrator;
  } else {
    return Invoker::Unknown;
  }
}

}  // namespace cuttlefish
