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
#include <string_view>
#include <vector>

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/libs/metrics/metrics_environment.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Flag EnvironmentGflagsCompatFlag(const std::string& name,
                                 ClearcutEnvironment& value) {
  return Flag::StringFlag(name)
      .Getter([&value]() { return EnvironmentToString(value); })
      .Setter([name, &value](std::string_view arg) -> Result<void> {
        if (arg == kClearcutLocal) {
          value = ClearcutEnvironment::Local;
        } else if (arg == kClearcutStaging) {
          value = ClearcutEnvironment::Staging;
        } else if (arg == kClearcutProduction) {
          value = ClearcutEnvironment::Production;
        } else {
          return CF_ERRF("Unexpected environment value: \"{}\"", arg);
        }
        return {};
      });
}

Flag Base64GflagsCompatFlag(const std::string& name, std::string& value) {
  return Flag::StringFlag(name)
      .Getter([&value]() { return value; })
      .Setter([name, &value](std::string_view arg) -> Result<void> {
        std::vector<uint8_t> decoded_out =
            CF_EXPECTF(DecodeBase64(std::string(arg)),
                       "Unable to base64 decode string: '{}'", arg);
        value = std::string(decoded_out.begin(), decoded_out.end());
        return {};
      });
}

}  // namespace

Result<MetricsFlags> ProcessFlags(int argc, char** argv) {
  MetricsFlags result;
  std::vector<Flag> flags;
  flags.emplace_back(
      EnvironmentGflagsCompatFlag("environment", result.environment)
          .Help("Specify the environment to transmit to."));
  flags.emplace_back(
      GflagsCompatFlag("event_filepath", result.event_filepath)
          .Help("Debug flag to provide a cvd-generated metrics event file "
                "instead of the serialized proto."));
  // base64 encoded to be passed as command argument without mangling the string
  flags.emplace_back(
      Base64GflagsCompatFlag("serialized_proto", result.serialized_proto)
          .Help("The base64 encoded, serialized proto string data to decode "
                "and transmit."));
  flags.emplace_back(HelpFlag(flags));
  std::vector<std::string> args(argv + 1, argv + argc);  // Skip argv[0]
  CF_EXPECT(ConsumeFlags(flags, args, {.fail_on_unexpected_argument = true}));

  CF_EXPECT(result.serialized_proto.empty() != result.event_filepath.empty(),
            "Must specify one and only one of the two input flags.  The event "
            "file is only intended for debugging the transmitter.");
  return result;
}

}  // namespace cuttlefish
