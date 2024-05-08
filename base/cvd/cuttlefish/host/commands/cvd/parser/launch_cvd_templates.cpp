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
#include "host/commands/cvd/parser/launch_cvd_templates.h"

#include <string>
#include <string_view>

#include <google/protobuf/util/json_util.h>
#include "json/json.h"

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

using google::protobuf::util::JsonStringToMessage;
using google::protobuf::util::MessageToJsonString;

namespace cuttlefish {

using cvd::config::Instance;
using cvd::config::Launch;

// Definition of phone instance template in Json format
static constexpr std::string_view kPhoneInstanceTemplate = R""""(
{
    "vm": {
        "memory_mb": 4096
    },
    "graphics":{
        "displays":[
            {
                "width": 720,
                "height": 1280,
                "dpi": 320
            }
        ]
    }
}
  )"""";

// Definition of tablet instance template in Json format
static constexpr std::string_view kTabletInstanceTemplate = R""""(
{
    "vm": {
        "memory_mb": 4096
    },
    "graphics":{
        "displays":[
            {
                "width": 2560,
                "height": 1800,
                "dpi": 320
            }
        ]
    }
}
  )"""";

// Definition of tablet instance template in Json format
static constexpr std::string_view kTvInstanceTemplate = R""""(
{
    "vm": {
        "memory_mb": 2048
    },
    "graphics":{
        "displays":[
            {
                "width": 1920,
                "height": 1080,
                "dpi": 213
            }
        ]
    }
}
  )"""";

// Definition of tablet instance template in Json format
static constexpr std::string_view kWearableInstanceTemplate = R""""(
{
    "vm": {
        "memory_mb": 1536,
        "use_sdcard" : false
    },
    "graphics":{
        "displays":[
            {
                "width": 450,
                "height": 450,
                "dpi": 320
            }
        ]
    }
}
  )"""";

// Definition of auto instance template in Json format
static constexpr std::string_view kAutoInstanceTemplate = R""""(
{
    "vm": {
        "memory_mb": 4096
    },
    "graphics":{
        "displays":[
            {
                "width": 1080,
                "height": 600,
                "dpi": 120
            },
            {
                "width": 400,
                "height": 600,
                "dpi": 120
            }
        ]
    }
}
  )"""";

// Definition of auto instance template in Json format
static constexpr std::string_view kSlimInstanceTemplate = R""""(
{
    "vm": {
        "memory_mb": 2048,
        "use_sdcard" : false
    },
    "graphics":{
        "displays":[
            {
                "width": 720,
                "height": 1280,
                "dpi": 320
            }
        ]
    }
}
  )"""";

// Definition of go instance template in Json format
static constexpr std::string_view kGoInstanceTemplate = R""""(
{
    "vm": {
        "memory_mb": 2048
    },
    "graphics":{
        "displays":[
            {
                "width": 720,
                "height": 1280,
                "dpi": 320
            }
        ]
    }
}
  )"""";

static constexpr std::string_view kFoldableInstanceTemplate = R""""(
{
    "vm": {
            "memory_mb": 4096,
            "custom_actions" : [
                    {
                            "device_states": [
                                    {
                                            "lid_switch_open": false,
                                            "hinge_angle_value": 0
                                    }
                            ],
                            "button":{
                                    "command":"device_state_closed",
                                    "title":"Device State Closed",
                                    "icon_name":"smartphone"
                            }
                    },
                    {
                            "device_states": [
                                    {
                                            "lid_switch_open": true,
                                            "hinge_angle_value": 90
                                    }
                            ],
                            "button":{
                                    "command":"device_state_half_opened",
                                    "title":"Device State Half-Opened",
                                    "icon_name":"laptop"
                            }
                    },
                    {
                            "device_states": [
                                    {
                                            "lid_switch_open": true,
                                            "hinge_angle_value": 180
                                    }
                            ],
                            "button":{
                                    "command":"device_state_opened",
                                    "title":"Device State Opened",
                                    "icon_name":"tablet"
                            }
                    }
            ]
    },
    "graphics":{
            "displays":[
                {
                    "width": 1768,
                    "height": 2208,
                    "dpi": 374
                },
                {
                    "width": 832,
                    "height": 2268,
                    "dpi": 387
                }
            ]
    }
}
  )"""";

static Result<Json::Value> LoadTemplateByName(const std::string& template_name) {
  static auto* kSupportedTemplatesKeyMap =
      new std::map<std::string_view, std::string_view>{
          {"phone", kPhoneInstanceTemplate},
          {"tablet", kTabletInstanceTemplate},
          {"tv", kTvInstanceTemplate},
          {"wearable", kWearableInstanceTemplate},
          {"auto", kAutoInstanceTemplate},
          {"slim", kSlimInstanceTemplate},
          {"go", kGoInstanceTemplate},
          {"foldable", kFoldableInstanceTemplate}};

  auto template_it = kSupportedTemplatesKeyMap->find(template_name);
  CF_EXPECTF(template_it != kSupportedTemplatesKeyMap->end(),
             "Unknown import value '{}'", template_name);

  return CF_EXPECT(ParseJson(template_it->second));
}

Result<Launch> ExtractLaunchTemplates(Launch config) {
  for (auto& ins : *config.mutable_instances()) {
    if (ins.has_import_template() && ins.import_template() != "") {
      auto tmpl_json = CF_EXPECT(LoadTemplateByName(ins.import_template()));
      // TODO: b/337089452 - handle repeated merges within protos
      // `proto.MergeFrom` concatenates repeated fields, but we want index-wise
      // merging of repeated fields.
      std::string ins_json_str;
      auto status = MessageToJsonString(ins, &ins_json_str);
      CF_EXPECTF(status.ok(), "{}", status.ToString());
      auto ins_json = CF_EXPECT(ParseJson(ins_json_str));

      MergeTwoJsonObjs(ins_json, tmpl_json);
      std::stringstream combined_json_sstream;
      combined_json_sstream << ins_json;
      const auto& combined_json_str = combined_json_sstream.str();

      //status = JsonStringToMessage(combined_json_str, &ins);
      CF_EXPECTF(status.ok(), "{}", status.ToString());
    }
  }
  return config;
}

}  // namespace cuttlefish
