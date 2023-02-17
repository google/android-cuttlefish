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

#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace cvd_common {

Args ConvertToArgs(
    const google::protobuf::RepeatedPtrField<std::string>& proto_args) {
  Args args;
  args.reserve(proto_args.size());
  for (const auto& proto_arg : proto_args) {
    args.emplace_back(proto_arg);
  }
  return args;
}

Envs ConvertToEnvs(
    const google::protobuf::Map<std::string, std::string>& proto_map) {
  cvd_common::Envs envs;
  for (const auto& entry : proto_map) {
    envs[entry.first] = entry.second;
  }
  return envs;
}

}  // namespace cvd_common
}  // namespace cuttlefish
