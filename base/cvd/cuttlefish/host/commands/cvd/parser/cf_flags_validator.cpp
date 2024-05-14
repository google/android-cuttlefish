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

#include "host/commands/cvd/parser/cf_flags_validator.h"

#include <google/protobuf/util/json_util.h>
#include "json/json.h"

#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"

namespace cuttlefish {

using cvd::config::EnvironmentSpecification;
using google::protobuf::util::JsonStringToMessage;

Result<EnvironmentSpecification> ValidateCfConfigs(const Json::Value& root) {
  std::stringstream json_as_stringstream;
  json_as_stringstream << root;
  auto json_str = json_as_stringstream.str();

  EnvironmentSpecification launch_config;
  auto status = JsonStringToMessage(json_str, &launch_config);
  CF_EXPECTF(status.ok(), "{}", status.ToString());

  return launch_config;
}

}  // namespace cuttlefish
