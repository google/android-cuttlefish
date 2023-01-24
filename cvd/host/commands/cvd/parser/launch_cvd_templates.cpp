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
#include <android-base/file.h>

#include <stdio.h>
#include <string>

#include "common/libs/utils/json.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {

enum class ConfigTemplate {
  PHONE,
  TABLET,
  TV,
  WEARABLE,
  AUTO,
  SLIM,
  GO,
  FOLDABLE,
  UNKNOWN,
};

static std::map<std::string, ConfigTemplate> kSupportedTemplatesKeyMap = {
    {"phone.json", ConfigTemplate::PHONE},
    {"tablet.json", ConfigTemplate::TABLET},
    {"tv.json", ConfigTemplate::TV},
    {"wearable.json", ConfigTemplate::WEARABLE},
    {"auto.json", ConfigTemplate::AUTO},
    {"slim.json", ConfigTemplate::SLIM},
    {"go.json", ConfigTemplate::GO},
    {"foldable.json", ConfigTemplate::FOLDABLE}};

// Definition of phone instance template in Json format
static const char* kPhoneInstanceTemplate = R""""(
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

// Definition of tablet instance template in Json format
static const char* kTabletInstanceTemplate = R""""(
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
static const char* kTvInstanceTemplate = R""""(
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
static const char* kWearableInstanceTemplate = R""""(
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
static const char* kAutoInstanceTemplate = R""""(
{
    "vm": {
        "memory_mb": 4069
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
static const char* kSlimInstanceTemplate = R""""(
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
static const char* kGoInstanceTemplate = R""""(
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

static const char* kFoldableInstanceTemplate = R""""(
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

Json::Value ExtractJsonTemplate(const Json::Value& instance,
                                const char* template_string) {
  std::string json_text(template_string);
  Json::Value result;

  Json::Reader reader;
  reader.parse(json_text, result);
  MergeJson(result, instance);
  return result;
}

Json::Value ExtractInstaneTemplate(const Json::Value& instance) {
  std::string instance_template = instance["@import"].asString();
  ConfigTemplate selected_template =
      kSupportedTemplatesKeyMap.at(instance_template);

  Json::Value result;

  switch (selected_template) {
    case ConfigTemplate::PHONE:
      // Extract phone instance configs from input template
      result = ExtractJsonTemplate(instance, kPhoneInstanceTemplate);
      break;
    case ConfigTemplate::TABLET:
      // Extract tablet instance configs from input template
      result = ExtractJsonTemplate(instance, kTabletInstanceTemplate);
      break;
    case ConfigTemplate::TV:
      // Extract tv instance configs from input template
      result = ExtractJsonTemplate(instance, kTvInstanceTemplate);
      break;
    case ConfigTemplate::WEARABLE:
      // Extract wearable instance configs from input template
      result = ExtractJsonTemplate(instance, kWearableInstanceTemplate);
      break;
    case ConfigTemplate::AUTO:
      // Extract auto instance configs from input template
      result = ExtractJsonTemplate(instance, kAutoInstanceTemplate);
      break;
    case ConfigTemplate::SLIM:
      // Extract slim instance configs from input template
      result = ExtractJsonTemplate(instance, kSlimInstanceTemplate);
      break;
    case ConfigTemplate::GO:
      // Extract go instance configs from input template
      result = ExtractJsonTemplate(instance, kGoInstanceTemplate);
      break;
    case ConfigTemplate::FOLDABLE:
      // Extract foldable instance configs from input template
      result = ExtractJsonTemplate(instance, kFoldableInstanceTemplate);
      break;

    default:
      // handle unsupported @import flag values
      result = instance;
      break;
  }

  return result;
}

void ExtractLaunchTemplates(Json::Value& root) {
  int num_instances = root.size();
  for (unsigned int i = 0; i < num_instances; i++) {
    // Validate @import flag values are supported or not
    if (root[i].isMember("@import")) {
      // Extract instance configs from input template and override current
      // instance
      root[i] = ExtractInstaneTemplate(root[i]);
    }
  }
}

}  // namespace cuttlefish
