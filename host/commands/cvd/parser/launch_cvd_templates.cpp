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

namespace cuttlefish {

enum class ConfigTemplate {
  PHONE,
  TABLET,
  TV,
  WEARABLE,
  AUTO,
  SLIM,
  GO,
  UNKNOWN,
};

static std::map<std::string, ConfigTemplate> kSupportedTemplatesKeyMap = {
    {"phone.json", ConfigTemplate::PHONE},
    {"tablet.json", ConfigTemplate::TABLET},
    {"tv.json", ConfigTemplate::TV},
    {"wearable.json", ConfigTemplate::WEARABLE},
    {"auto.json", ConfigTemplate::AUTO},
    {"slim.json", ConfigTemplate::SLIM},
    {"go.json", ConfigTemplate::GO}};

void ExtractInstaneTemplate(Json::Value&,
                            const std::string& instance_template) {
  ConfigTemplate selected_template =
      kSupportedTemplatesKeyMap.at(instance_template);

  switch (selected_template) {
    case ConfigTemplate::PHONE:
      // Extract phone instance configs from input template
      // TODO: Add code to extract phone instance configs from input template
      break;
    case ConfigTemplate::TABLET:
      // Extract tablet instance configs from input template
      // TODO: Add code to extract tablet instance configs from input template
      break;
    case ConfigTemplate::TV:
      // Extract tv instance configs from input template
      // TODO: Add code to extract tv instance configs from input template
      break;
    case ConfigTemplate::WEARABLE:
      // Extract wearable instance configs from input template
      // TODO: Add code to extract wearable instance configs from input template
      break;
    case ConfigTemplate::AUTO:
      // Extract auto instance configs from input template
      // TODO: Add code to extract auto instance configs from input template
      break;
    case ConfigTemplate::SLIM:
      // Extract slim instance configs from input template
      // TODO: Add code to extract slim instance configs from input template
      break;
    case ConfigTemplate::GO:
      // Extract go instance configs from input template
      // TODO Add code to extract go instance configs from input template
      break;

    default:
      // Extract instance configs from input template
      break;
  }
}

void ExtractLaunchTemplates(Json::Value& root) {
  int num_instances = root.size();
  for (unsigned int i = 0; i < num_instances; i++) {
    // Validate @import flag values are supported or not
    if (root[i].isMember("@import")) {
      // Extract instance configs from input template
      ExtractInstaneTemplate(root[i], root[i]["@import"].asString());
    }
  }
}

}  // namespace cuttlefish
