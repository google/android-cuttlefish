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
#include "host/commands/cvd/parser/instance/cf_graphics_configs.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <google/protobuf/text_format.h>

#include "cuttlefish/host/commands/cvd/parser/instance/launch_cvd.pb.h"

#include "common/libs/utils/base64.h"
#include "common/libs/utils/result.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"

namespace cuttlefish {

using cvd::config::Instance;
using cvd::config::Launch;

Result<void> InitGraphicsConfigs(Json::Value& instances) {
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "width",
                              CF_DEFAULTS_DISPLAY_WIDTH);
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "height",
                              CF_DEFAULTS_DISPLAY_HEIGHT);
  InitIntConfigSubGroupVector(instances, "graphics", "displays", "dpi",
                              CF_DEFAULTS_DISPLAY_DPI);
  InitIntConfigSubGroupVector(instances, "graphics", "displays",
                              "refresh_rate_hertz",
                              CF_DEFAULTS_DISPLAY_REFRESH_RATE);
  for (auto& instance : instances) {
    CF_EXPECT(InitConfig(instance, CF_DEFAULTS_RECORD_SCREEN,
                         {"graphics", "record_screen"}));
  }
  return {};
}

Result<std::string> GenerateDisplayFlag(const Launch& cfg) {
  cuttlefish::InstancesDisplays all_instances_displays;

  for (const auto& in_instance : cfg.instances()) {
    auto& out_instance = *all_instances_displays.add_instances();
    for (const auto& in_display : in_instance.graphics().displays()) {
      auto& out_display = *out_instance.add_displays();
      out_display.set_width(in_display.width());
      out_display.set_height(in_display.height());
      out_display.set_dpi(in_display.dpi());
      out_display.set_refresh_rate_hertz(in_display.refresh_rate_hertz());
    }
  }

  std::string bin_output;
  CF_EXPECT(all_instances_displays.SerializeToString(&bin_output),
            "Failed to convert display proto to binary string ");

  std::string base64_output;
  CF_EXPECT(EncodeBase64(bin_output.data(), bin_output.size(), &base64_output),
            "Failed to apply EncodeBase64 to binary string ");

  return "--displays_binproto=" + base64_output;
}

bool RecordScreen(const Instance& instance) {
  return instance.graphics().record_screen();
}

Result<std::vector<std::string>> GenerateGraphicsFlags(const Launch& cfg) {
  return std::vector<std::string>{
      CF_EXPECT(GenerateDisplayFlag(cfg)),
      GenerateInstanceFlag("record_screen", cfg, RecordScreen),
  };
}

}  // namespace cuttlefish
