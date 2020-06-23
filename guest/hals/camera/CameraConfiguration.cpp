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
#include "CameraConfiguration.h"

#define LOG_TAG "CameraConfiguration"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <log/log.h>
#include <json/json.h>
#include <json/reader.h>
#include <stdlib.h>

namespace cuttlefish {
namespace {
////////////////////// Device Personality keys //////////////////////
//
// **** Camera ****
//
// Example segment (transcribed to constants):
//
// kCameraDefinitionsKey: [
//   {
//     kCameraDefinitionOrientationKey: "front",
//     kCameraDefinitionHalVersionKey: "1",
//     kCameraDefinitionResolutionsKey: [
//       {
//         kCameraDefinitionResolutionWidthKey: "1600",
//         kCameraDefinitionResolutionHeightKey: "1200",
//       },
//       {
//         kCameraDefinitionResolutionWidthKey: "1280",
//         kCameraDefinitionResolutionHeightKey: "800",
//       }
//     ]
//   },
//   {
//     kCameraDefinitionOrientationKey: "back",
//     kCameraDefinitionHalVersionKey: "1",
//     kCameraDefinitionResolutionsKey: [
//       {
//         kCameraDefinitionResolutionWidthKey: "1024",
//         kCameraDefinitionResolutionHeightKey: "768",
//       },
//       {
//         kCameraDefinitionResolutionWidthKey: "800",
//         kCameraDefinitionResolutionHeightKey: "600",
//       }
//     ]
//   }
// ]
//

// Location of the camera configuration files.
const char* const kConfigurationFileLocation = "/vendor/etc/config/camera.json";

//
// Array of camera definitions for all cameras available on the device (array).
// Top Level Key.
const char* const kCameraDefinitionsKey = "camera_definitions";

// Camera orientation of currently defined camera (string).
// Currently supported values:
// - "back",
// - "front".
const char* const kCameraDefinitionOrientationKey = "orientation";

// Camera HAL version of currently defined camera (int).
// Currently supported values:
// - 1 (Camera HALv1)
// - 2 (Camera HALv2)
// - 3 (Camera HALv3)
const char* const kCameraDefinitionHalVersionKey = "hal_version";

// Array of resolutions supported by camera (array).
const char* const kCameraDefinitionResolutionsKey = "resolutions";

// Width of currently defined resolution (int).
// Must be divisible by 8.
const char* const kCameraDefinitionResolutionWidthKey = "width";

// Height of currently defined resolution (int).
// Must be divisible by 8.
const char* const kCameraDefinitionResolutionHeightKey = "height";

// Convert string value to camera orientation.
bool ValueToCameraOrientation(const std::string& value,
                              CameraDefinition::Orientation* orientation) {
  if (value == "back") {
    *orientation = CameraDefinition::kBack;
    return true;
  } else if (value == "front") {
    *orientation = CameraDefinition::kFront;
    return true;
  }
  ALOGE("%s: Invalid camera orientation: %s.", __FUNCTION__, value.c_str());
  return false;
}

// Convert string value to camera HAL version.
bool ValueToCameraHalVersion(const std::string& value,
                             CameraDefinition::HalVersion* hal_version) {
  int temp;
  char* endptr;

  temp = strtol(value.c_str(), &endptr, 10);
  if (endptr != value.c_str() + value.size()) {
    ALOGE("%s: Invalid camera HAL version. Expected number, got %s.",
          __FUNCTION__, value.c_str());
    return false;
  }

  switch (temp) {
    case 1:
      *hal_version = CameraDefinition::kHalV1;
      break;

    case 2:
      *hal_version = CameraDefinition::kHalV2;
      break;

    case 3:
      *hal_version = CameraDefinition::kHalV3;
      break;

    default:
      ALOGE("%s: Invalid camera HAL version. Version %d not supported.",
            __FUNCTION__, temp);
      return false;
  }

  return true;
}

bool ValueToCameraResolution(const std::string& width,
                             const std::string& height,
                             CameraDefinition::Resolution* resolution) {
  char* endptr;

  resolution->width = strtol(width.c_str(), &endptr, 10);
  if (endptr != width.c_str() + width.size()) {
    ALOGE("%s: Invalid camera resolution width. Expected number, got %s.",
          __FUNCTION__, width.c_str());
    return false;
  }

  resolution->height = strtol(height.c_str(), &endptr, 10);
  if (endptr != height.c_str() + height.size()) {
    ALOGE("%s: Invalid camera resolution height. Expected number, got %s.",
          __FUNCTION__, height.c_str());
    return false;
  }

  // Validate width and height parameters are sane.
  if (resolution->width <= 0 || resolution->height <= 0) {
    ALOGE("%s: Invalid camera resolution: %dx%d", __FUNCTION__,
          resolution->width, resolution->height);
    return false;
  }

  // Validate width and height divisible by 8.
  if ((resolution->width & 7) != 0 || (resolution->height & 7) != 0) {
    ALOGE(
        "%s: Invalid camera resolution: width and height must be "
        "divisible by 8, got %dx%d (%dx%d).",
        __FUNCTION__, resolution->width, resolution->height,
        resolution->width & 7, resolution->height & 7);
    return false;
  }

  return true;
}

// Process camera definitions.
// Returns true, if definitions were sane.
bool ConfigureCameras(const Json::Value& value,
                      std::vector<CameraDefinition>* cameras) {
  if (!value.isObject()) {
    ALOGE("%s: Configuration root is not an object", __FUNCTION__);
    return false;
  }

  if (!value.isMember(kCameraDefinitionsKey)) return true;
  for (Json::ValueConstIterator iter = value[kCameraDefinitionsKey].begin();
       iter != value[kCameraDefinitionsKey].end(); ++iter) {
    cameras->push_back(CameraDefinition());
    CameraDefinition& camera = cameras->back();

    if (!iter->isObject()) {
      ALOGE("%s: Camera definition is not an object", __FUNCTION__);
      continue;
    }

    // Camera without orientation -> invalid setting.
    if (!iter->isMember(kCameraDefinitionOrientationKey)) {
      ALOGE("%s: Invalid camera definition: key %s is missing.", __FUNCTION__,
            kCameraDefinitionOrientationKey);
      return false;
    }

    if (!ValueToCameraOrientation(
            (*iter)[kCameraDefinitionOrientationKey].asString(),
            &camera.orientation))
      return false;

    // Camera without HAL version -> invalid setting.
    if (!(*iter).isMember(kCameraDefinitionHalVersionKey)) {
      ALOGE("%s: Invalid camera definition: key %s is missing.", __FUNCTION__,
            kCameraDefinitionHalVersionKey);
      return false;
    }

    if (!ValueToCameraHalVersion(
            (*iter)[kCameraDefinitionHalVersionKey].asString(),
            &camera.hal_version))
      return false;

    // Camera without resolutions -> invalid setting.
    if (!iter->isMember(kCameraDefinitionResolutionsKey)) {
      ALOGE("%s: Invalid camera definition: key %s is missing.", __FUNCTION__,
            kCameraDefinitionResolutionsKey);
      return false;
    }

    const Json::Value& json_resolutions =
        (*iter)[kCameraDefinitionResolutionsKey];

    // Resolutions not an array, or an empty array -> invalid setting.
    if (!json_resolutions.isArray() || json_resolutions.empty()) {
      ALOGE("%s: Invalid camera definition: %s is not an array or is empty.",
            __FUNCTION__, kCameraDefinitionResolutionsKey);
      return false;
    }

    // Process all resolutions.
    for (Json::ValueConstIterator json_res_iter = json_resolutions.begin();
         json_res_iter != json_resolutions.end(); ++json_res_iter) {
      // Check presence of width and height keys.
      if (!json_res_iter->isObject()) {
        ALOGE("%s: Camera resolution item is not an object", __FUNCTION__);
        continue;
      }
      if (!json_res_iter->isMember(kCameraDefinitionResolutionWidthKey) ||
          !json_res_iter->isMember(kCameraDefinitionResolutionHeightKey)) {
        ALOGE(
            "%s: Invalid camera resolution: keys %s and %s are both required.",
            __FUNCTION__, kCameraDefinitionResolutionWidthKey,
            kCameraDefinitionResolutionHeightKey);
        return false;
      }

      camera.resolutions.push_back(CameraDefinition::Resolution());
      CameraDefinition::Resolution& resolution = camera.resolutions.back();

      if (!ValueToCameraResolution(
              (*json_res_iter)[kCameraDefinitionResolutionWidthKey].asString(),
              (*json_res_iter)[kCameraDefinitionResolutionHeightKey].asString(),
              &resolution))
        return false;
    }
  }

  return true;
}
}  // namespace

bool CameraConfiguration::Init() {
  cameras_.clear();
  std::string config;
  if (!android::base::ReadFileToString(kConfigurationFileLocation, &config)) {
    ALOGE("%s: Could not open configuration file: %s", __FUNCTION__,
          kConfigurationFileLocation);
    return false;
  }

  Json::Reader config_reader;
  Json::Value root;
  if (!config_reader.parse(config, root)) {
    ALOGE("Could not parse configuration file: %s",
          config_reader.getFormattedErrorMessages().c_str());
    return false;
  }

  return ConfigureCameras(root, &cameras_);
}

}  // namespace cuttlefish
