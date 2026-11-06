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

#include "cuttlefish/metrics/host/kokoro_environment.h"

#include <optional>

#include "absl/strings/string_view.h"

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/metrics/host/gce_environment.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr absl::string_view kKokoroBuildId = "KOKORO_BUILD_ID";

}  // namespace

Result<std::optional<KokoroEnvironment>> DetectKokoroEnvironment() {
  if (!StringFromEnv(kKokoroBuildId)) {
    return std::nullopt;
  }
  std::optional<GceEnvironment> gce_env = CF_EXPECT(DetectGceEnvironment());
  if (!gce_env) {
    return std::nullopt;
  }
  return KokoroEnvironment{
      .numeric_project_id = gce_env->numeric_project_id,
      .zone = gce_env->zone,
  };
}

}  // namespace cuttlefish
