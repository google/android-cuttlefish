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

#include "host/libs/graphics_detector/graphics_configuration.h"

#include <ostream>

#include <android-base/strings.h>

#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

struct AngleFeatures {
  // Prefer linear filtering for YUV AHBs to pass
  // android.media.decoder.cts.DecodeAccuracyTest.
  bool prefer_linear_filtering_for_yuv = true;

  // Map unspecified color spaces to PASS_THROUGH to pass
  // android.media.codec.cts.DecodeEditEncodeTest and
  // android.media.codec.cts.EncodeDecodeTest.
  bool map_unspecified_color_space_to_pass_through = true;

  // b/264575911: Nvidia seems to have issues with YUV samplers with
  // 'lowp' and 'mediump' precision qualifiers.
  bool ignore_precision_qualifiers = false;
};

std::ostream& operator<<(std::ostream& stream, const AngleFeatures& features) {
  std::ios_base::fmtflags flags_backup(stream.flags());
  stream << std::boolalpha;
  stream << "ANGLE features: "
         << "\n";
  stream << " - prefer_linear_filtering_for_yuv: "
         << features.prefer_linear_filtering_for_yuv << "\n";
  stream << " - map_unspecified_color_space_to_pass_through: "
         << features.map_unspecified_color_space_to_pass_through << "\n";
  stream << " - ignore_precision_qualifiers: "
         << features.ignore_precision_qualifiers << "\n";
  stream.flags(flags_backup);
  return stream;
}

AngleFeatures GetNeededAngleFeaturesBasedOnQuirks(
    const RenderingMode mode, const GraphicsAvailability& availability) {
  AngleFeatures features = {};
  switch (mode) {
    case RenderingMode::kGfxstream:
      break;
    case RenderingMode::kGfxstreamGuestAngle: {
      if (availability
              .vulkan_has_issue_with_precision_qualifiers_on_yuv_samplers) {
        features.ignore_precision_qualifiers = true;
      }
      break;
    }
    case RenderingMode::kGuestSwiftShader:
    case RenderingMode::kVirglRenderer:
    case RenderingMode::kNone:
      break;
  }
  return features;
}

}  // namespace

Result<RenderingMode> GetRenderingMode(const std::string& mode) {
  if (mode == std::string(kGpuModeDrmVirgl)) {
    return RenderingMode::kVirglRenderer;
  }
  if (mode == std::string(kGpuModeGfxstream)) {
    return RenderingMode::kGfxstream;
  }
  if (mode == std::string(kGpuModeGfxstreamGuestAngle)) {
    return RenderingMode::kGfxstreamGuestAngle;
  }
  if (mode == std::string(kGpuModeGuestSwiftshader)) {
    return RenderingMode::kGuestSwiftShader;
  }
  if (mode == std::string(kGpuModeNone)) {
    return RenderingMode::kNone;
  }
  return CF_ERR("Unsupported rendering mode: " << mode);
}

Result<AngleFeatureOverrides> GetNeededAngleFeatures(
    const RenderingMode mode, const GraphicsAvailability& availability) {
  const AngleFeatures features =
      GetNeededAngleFeaturesBasedOnQuirks(mode, availability);
  LOG(DEBUG) << features;

  std::vector<std::string> enable_feature_strings;
  std::vector<std::string> disable_feature_strings;
  if (features.prefer_linear_filtering_for_yuv) {
    enable_feature_strings.push_back("preferLinearFilterForYUV");
  }
  if (features.map_unspecified_color_space_to_pass_through) {
    enable_feature_strings.push_back("mapUnspecifiedColorSpaceToPassThrough");
  }
  if (features.ignore_precision_qualifiers) {
    disable_feature_strings.push_back("enablePrecisionQualifiers");
  }

  return AngleFeatureOverrides{
      .angle_feature_overrides_enabled =
          android::base::Join(enable_feature_strings, ':'),
      .angle_feature_overrides_disabled =
          android::base::Join(disable_feature_strings, ':'),
  };
}

}  // namespace cuttlefish