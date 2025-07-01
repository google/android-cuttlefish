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
#include "cuttlefish/host/commands/assemble_cvd/flags/display_proto.h"

#include <string>
#include <vector>

#include <fmt/format.h>
#include <google/protobuf/text_format.h>

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/commands/assemble_cvd/proto/launch_cvd.pb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

template <typename ProtoType>
Result<ProtoType> ParseTextProtoFlagHelper(const std::string& flag_value,
                                           const std::string& flag_name) {
  ProtoType proto_result;
  google::protobuf::TextFormat::Parser p;
  CF_EXPECT(p.ParseFromString(flag_value, &proto_result),
            "Failed to parse: " << flag_name << ", value: " << flag_value);
  return proto_result;
}

template <typename ProtoType>
Result<ProtoType> ParseBinProtoFlagHelper(const std::string& flag_value,
                                          const std::string& flag_name) {
  ProtoType proto_result;
  std::vector<uint8_t> output;
  CF_EXPECT(DecodeBase64(flag_value, &output));
  std::string serialized = std::string(output.begin(), output.end());
  CF_EXPECT(proto_result.ParseFromString(serialized),
            "Failed to parse binary proto, flag: " << flag_name << ", value: "
                                                   << flag_value);
  return proto_result;
}

}  // namespace

Result<std::vector<std::vector<CuttlefishConfig::DisplayConfig>>>
ParseDisplaysProto() {
  auto proto_result = FLAGS_displays_textproto.empty()
                          ? ParseBinProtoFlagHelper<InstancesDisplays>(
                                FLAGS_displays_binproto, "displays_binproto")
                          : ParseTextProtoFlagHelper<InstancesDisplays>(
                                FLAGS_displays_textproto, "displays_textproto");

  InstancesDisplays display_proto = CF_EXPECT(std::move(proto_result));

  std::vector<std::vector<CuttlefishConfig::DisplayConfig>> result;
  for (int i = 0; i < display_proto.instances_size(); i++) {
    std::vector<CuttlefishConfig::DisplayConfig> display_configs;
    const InstanceDisplays& launch_cvd_instance = display_proto.instances(i);
    for (int display_num = 0; display_num < launch_cvd_instance.displays_size();
         display_num++) {
      const InstanceDisplay& display =
          launch_cvd_instance.displays(display_num);

      // use same code logic from ParseDisplayConfig
      int display_dpi = CF_DEFAULTS_DISPLAY_DPI;
      if (display.dpi() != 0) {
        display_dpi = display.dpi();
      }

      int display_refresh_rate_hz = CF_DEFAULTS_DISPLAY_REFRESH_RATE;
      if (display.refresh_rate_hertz() != 0) {
        display_refresh_rate_hz = display.refresh_rate_hertz();
      }

      std::string overlays = "";

      for (const auto& overlay : display.overlays()) {
        overlays +=
            fmt::format("{}:{} ", overlay.vm_index(), overlay.display_index());
      }

      auto dc = CuttlefishConfig::DisplayConfig{
          .width = display.width(),
          .height = display.height(),
          .dpi = display_dpi,
          .refresh_rate_hz = display_refresh_rate_hz,
          .overlays = overlays,
      };

      display_configs.push_back(dc);
    }
    result.push_back(display_configs);
  }

  return result;
}

}  // namespace cuttlefish
