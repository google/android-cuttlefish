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

using cvd::config::Display;
using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;

Result<std::string> GenerateDisplayFlag(const EnvironmentSpecification& cfg) {
  cuttlefish::InstancesDisplays all_instances_displays;

  for (const auto& in_instance : cfg.instances()) {
    auto& out_instance = *all_instances_displays.add_instances();

    auto& in_disp_raw = in_instance.graphics().displays();
    std::vector<Display> in_displays(in_disp_raw.begin(), in_disp_raw.end());
    if (in_displays.empty()) {
      in_displays.emplace_back();  // At least one display, with default values
    }

    for (const auto& in_display : in_displays) {
      auto& out_display = *out_instance.add_displays();
      if (in_display.has_width()) {
        out_display.set_width(in_display.width());
      } else {
        out_display.set_width(CF_DEFAULTS_DISPLAY_WIDTH);
      }
      if (in_display.has_height()) {
        out_display.set_height(in_display.height());
      } else {
        out_display.set_height(CF_DEFAULTS_DISPLAY_HEIGHT);
      }
      if (in_display.has_dpi()) {
        out_display.set_dpi(in_display.dpi());
      } else {
        out_display.set_dpi(CF_DEFAULTS_DISPLAY_DPI);
      }
      if (in_display.has_refresh_rate_hertz()) {
        out_display.set_refresh_rate_hertz(in_display.refresh_rate_hertz());
      } else {
        out_display.set_refresh_rate_hertz(CF_DEFAULTS_DISPLAY_REFRESH_RATE);
      }
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
  if (instance.graphics().has_record_screen()) {
    return instance.graphics().record_screen();
  } else {
    return CF_DEFAULTS_RECORD_SCREEN;
  }
}

Result<std::vector<std::string>> GenerateGraphicsFlags(
    const EnvironmentSpecification& cfg) {
  return std::vector<std::string>{
      CF_EXPECT(GenerateDisplayFlag(cfg)),
      GenerateInstanceFlag("record_screen", cfg, RecordScreen),
  };
}

}  // namespace cuttlefish
