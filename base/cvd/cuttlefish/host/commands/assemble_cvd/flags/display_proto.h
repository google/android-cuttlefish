/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

class DisplaysProtoFlag {
 public:
  static Result<DisplaysProtoFlag> FromGlobalGflags();

  // An empty value implies the flag is not used, while a present empty list
  // implies the user has explicitly requested an empty display list.
  const std::optional<
      std::vector<std::vector<CuttlefishConfig::DisplayConfig>>>&
  Config() const;

 private:
  explicit DisplaysProtoFlag(
      std::optional<std::vector<std::vector<CuttlefishConfig::DisplayConfig>>>);
  std::optional<std::vector<std::vector<CuttlefishConfig::DisplayConfig>>>
      config_;
};

}  // namespace cuttlefish
