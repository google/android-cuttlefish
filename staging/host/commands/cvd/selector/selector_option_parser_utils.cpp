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

#include "host/commands/cvd/selector/selector_option_parser_utils.h"

#include <android-base/strings.h>

#include "host/commands/cvd/selector/instance_database_utils.h"

namespace cuttlefish {
namespace selector {

Result<void> VerifyNameOptions(const VerifyNameOptionsParam& param) {
  const std::optional<std::string>& name = param.name;
  const std::optional<std::string>& device_name = param.device_name;
  const std::optional<std::string>& group_name = param.group_name;
  const std::optional<std::string>& per_instance_name = param.per_instance_name;
  if (name) {
    CF_EXPECT(!device_name && !group_name && !per_instance_name);
    return {};
  }
  if (device_name) {
    CF_EXPECT(!group_name && !per_instance_name);
  }
  return {};
}

Result<DeviceName> SplitDeviceName(const std::string& device_name) {
  CF_EXPECT(IsValidDeviceName(device_name),
            "\"" << device_name << "\" is not a valid device name.");
  auto tokens = android::base::Split(device_name, "-");
  // guaranteed by IsValidDeviceName() for now
  CF_EXPECT(tokens.size() == 2);
  return DeviceName{.group_name = tokens.front(),
                    .per_instance_name = tokens.back()};
}

Result<std::vector<std::string>> SeparateButWithNoEmptyToken(
    const std::string& input, const std::string& delimiter) {
  auto tokens = android::base::Split(input, delimiter);
  for (const auto& t : tokens) {
    CF_EXPECT(!t.empty());
  }
  return tokens;
}

}  // namespace selector
}  // namespace cuttlefish
