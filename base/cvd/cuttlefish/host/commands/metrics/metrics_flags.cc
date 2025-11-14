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

#include <cstdint>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/metrics/metrics_transmission.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Flag EnvironmentGflagsCompatFlag(const std::string& name,
                                 ClearcutEnvironment& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return EnvironmentToString(value); })
      .Setter([name, &value](const FlagMatch& match) -> Result<void> {
        if (match.value == "local") {
          value = ClearcutEnvironment::Local;
        } else if (match.value == "staging") {
          value = ClearcutEnvironment::Staging;
        } else if (match.value == "prod") {
          value = ClearcutEnvironment::Prod;
        } else {
          return CF_ERRF("Unexpected environment value: \"{}\"", match.value);
        }
        return {};
      });
}

Flag Base64GflagsCompatFlag(const std::string& name, std::string& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return value; })
      .Setter([name, &value](const FlagMatch& match) -> Result<void> {
        std::vector<uint8_t> decoded_out;
        CF_EXPECTF(DecodeBase64(match.value, &decoded_out),
                   "Unable to base64 decode string: {}", match.value);
        value = std::string(decoded_out.begin(), decoded_out.end());
        return {};
      });
}

}  // namespace

// TODO: chadreynolds - add debug flag to specify metrics file and transmit
//    for convenient use with different transmission environments

Result<MetricsFlags> ProcessFlags(int argc, char** argv) {
  MetricsFlags result;
  std::vector<Flag> flags;
  flags.emplace_back(
      EnvironmentGflagsCompatFlag("environment", result.environment)
          .Help("Specify the environment to transmit to."));
  // base64 encoded to be passed as command argument without mangling the string
  flags.emplace_back(
      Base64GflagsCompatFlag("serialized_proto", result.serialized_proto)
          .Help("The base64 encoded, serialized proto string data to decode "
                "and transmit."));
  flags.emplace_back(HelpFlag(flags));
  flags.emplace_back(UnexpectedArgumentGuard());
  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CF_EXPECT(ConsumeFlags(flags, args));
  return result;
}

}  // namespace cuttlefish
