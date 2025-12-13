/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/metrics/metrics_flags.h"

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/metrics/metrics_environment.h"

namespace cuttlefish {
namespace {

Flag EnvironmentGflagsCompatFlag(const std::string& name,
                                 ClearcutEnvironment& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return EnvironmentToString(value); })
      .Setter([name, &value](const FlagMatch& match) -> Result<void> {
        if (match.value == kLocal) {
          value = ClearcutEnvironment::Local;
        } else if (match.value == kStaging) {
          value = ClearcutEnvironment::Staging;
        } else if (match.value == kProduction) {
          value = ClearcutEnvironment::Production;
        } else {
          CF_ERRF("Unexpected environment value: \"{}\"", match.value);
        }
        return {};
      });
}

}  // namespace

// TODO: chadreynolds - add path flag to send logs to metrics/metrics.log
// TODO: chadreynolds - add debug flag to specify metrics file and transmit
//    for convenient use with different transmission environments

Result<MetricsFlags> ProcessFlags(int argc, char** argv) {
  MetricsFlags result;
  std::vector<Flag> flags;
  flags.emplace_back(
      EnvironmentGflagsCompatFlag("environment", result.environment)
          .Help("Specify the environment to transmit to."));
  flags.emplace_back(
      GflagsCompatFlag("serialized_proto", result.serialized_proto)
          .Help("The serialized proto data to transmit."));
  flags.emplace_back(HelpFlag(flags));
  flags.emplace_back(UnexpectedArgumentGuard());
  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CF_EXPECT(ConsumeFlags(flags, args));
  return result;
}

}  // namespace cuttlefish
