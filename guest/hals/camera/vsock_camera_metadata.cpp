/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include "vsock_camera_metadata.h"

#include <hardware/camera3.h>
#include <utils/misc.h>
#include <vector>

namespace android::hardware::camera::device::V3_4::implementation {

namespace {
// Mostly copied from ExternalCameraDevice
const uint8_t kHardwarelevel = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL;
const uint8_t kAberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
const uint8_t kAvailableAberrationModes[] = {
    ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF};
const int32_t kExposureCompensation = 0;
const uint8_t kAntibandingMode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
const int32_t kControlMaxRegions[] = {/*AE*/ 0, /*AWB*/ 0, /*AF*/ 0};
const uint8_t kVideoStabilizationMode =
    ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
const uint8_t kAwbAvailableMode = ANDROID_CONTROL_AWB_MODE_AUTO;
const uint8_t kAePrecaptureTrigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
const uint8_t kAeAvailableMode = ANDROID_CONTROL_AE_MODE_ON;
const uint8_t kAvailableFffect = ANDROID_CONTROL_EFFECT_MODE_OFF;
const uint8_t kControlMode = ANDROID_CONTROL_MODE_AUTO;
const uint8_t kControlAvailableModes[] = {ANDROID_CONTROL_MODE_OFF,
                                          ANDROID_CONTROL_MODE_AUTO};
const uint8_t kEdgeMode = ANDROID_EDGE_MODE_OFF;
const uint8_t kFlashInfo = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
const uint8_t kFlashMode = ANDROID_FLASH_MODE_OFF;
const uint8_t kHotPixelMode = ANDROID_HOT_PIXEL_MODE_OFF;
const uint8_t kJpegQuality = 90;
const int32_t kJpegOrientation = 0;
const int32_t kThumbnailSize[] = {240, 180};
const int32_t kJpegAvailableThumbnailSizes[] = {0, 0, 240, 180};
const uint8_t kFocusDistanceCalibration =
    ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED;
const uint8_t kOpticalStabilizationMode =
    ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
const uint8_t kFacing = ANDROID_LENS_FACING_EXTERNAL;
const float kLensMinFocusDistance = 0.0f;
const uint8_t kNoiseReductionMode = ANDROID_NOISE_REDUCTION_MODE_OFF;
const int32_t kPartialResultCount = 1;
const uint8_t kRequestPipelineMaxDepth = 4;
const int32_t kRequestMaxNumInputStreams = 0;
const float kScalerAvailableMaxDigitalZoom[] = {1};
const uint8_t kCroppingType = ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY;
const int32_t kTestPatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
const int32_t kTestPatternModes[] = {ANDROID_SENSOR_TEST_PATTERN_MODE_OFF};
const uint8_t kTimestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
const int32_t kOrientation = 0;
const uint8_t kAvailableShadingMode = ANDROID_SHADING_MODE_OFF;
const uint8_t kFaceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
const int32_t kMaxFaceCount = 0;
const uint8_t kAvailableHotpixelMode =
    ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
const uint8_t kLensShadingMapMode =
    ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
const int32_t kMaxLatency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
const int32_t kControlAeCompensationRange[] = {0, 0};
const camera_metadata_rational_t kControlAeCompensationStep[] = {{0, 1}};
const uint8_t kAfTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
const uint8_t kAfMode = ANDROID_CONTROL_AF_MODE_OFF;
const uint8_t kAfAvailableModes[] = {ANDROID_CONTROL_AF_MODE_OFF};
const uint8_t kAvailableSceneMode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
const uint8_t kAeLockAvailable = ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
const uint8_t kAwbLockAvailable = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
const int32_t kHalFormats[] = {HAL_PIXEL_FORMAT_BLOB,
                               HAL_PIXEL_FORMAT_YCbCr_420_888,
                               HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED};
const int32_t kRequestMaxNumOutputStreams[] = {
    /*RAW*/ 0,
    /*Processed*/ 2,
    /*Stall*/ 1};
const uint8_t kAvailableCapabilities[] = {
    ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE};
const int32_t kAvailableRequestKeys[] = {
    ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
    ANDROID_CONTROL_AE_ANTIBANDING_MODE,
    ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
    ANDROID_CONTROL_AE_LOCK,
    ANDROID_CONTROL_AE_MODE,
    ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
    ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
    ANDROID_CONTROL_AF_MODE,
    ANDROID_CONTROL_AF_TRIGGER,
    ANDROID_CONTROL_AWB_LOCK,
    ANDROID_CONTROL_AWB_MODE,
    ANDROID_CONTROL_CAPTURE_INTENT,
    ANDROID_CONTROL_EFFECT_MODE,
    ANDROID_CONTROL_MODE,
    ANDROID_CONTROL_SCENE_MODE,
    ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
    ANDROID_FLASH_MODE,
    ANDROID_JPEG_ORIENTATION,
    ANDROID_JPEG_QUALITY,
    ANDROID_JPEG_THUMBNAIL_QUALITY,
    ANDROID_JPEG_THUMBNAIL_SIZE,
    ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
    ANDROID_NOISE_REDUCTION_MODE,
    ANDROID_SCALER_CROP_REGION,
    ANDROID_SENSOR_TEST_PATTERN_MODE,
    ANDROID_STATISTICS_FACE_DETECT_MODE,
    ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE};
const int32_t kAvailableResultKeys[] = {
    ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
    ANDROID_CONTROL_AE_ANTIBANDING_MODE,
    ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
    ANDROID_CONTROL_AE_LOCK,
    ANDROID_CONTROL_AE_MODE,
    ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
    ANDROID_CONTROL_AE_STATE,
    ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
    ANDROID_CONTROL_AF_MODE,
    ANDROID_CONTROL_AF_STATE,
    ANDROID_CONTROL_AF_TRIGGER,
    ANDROID_CONTROL_AWB_LOCK,
    ANDROID_CONTROL_AWB_MODE,
    ANDROID_CONTROL_AWB_STATE,
    ANDROID_CONTROL_CAPTURE_INTENT,
    ANDROID_CONTROL_EFFECT_MODE,
    ANDROID_CONTROL_MODE,
    ANDROID_CONTROL_SCENE_MODE,
    ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
    ANDROID_FLASH_MODE,
    ANDROID_FLASH_STATE,
    ANDROID_JPEG_ORIENTATION,
    ANDROID_JPEG_QUALITY,
    ANDROID_JPEG_THUMBNAIL_QUALITY,
    ANDROID_JPEG_THUMBNAIL_SIZE,
    ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
    ANDROID_NOISE_REDUCTION_MODE,
    ANDROID_REQUEST_PIPELINE_DEPTH,
    ANDROID_SCALER_CROP_REGION,
    ANDROID_SENSOR_TIMESTAMP,
    ANDROID_STATISTICS_FACE_DETECT_MODE,
    ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
    ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,
    ANDROID_STATISTICS_SCENE_FLICKER};
const int32_t kAvailableCharacteristicsKeys[] = {
    ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
    ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
    ANDROID_CONTROL_AE_AVAILABLE_MODES,
    ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
    ANDROID_CONTROL_AE_COMPENSATION_RANGE,
    ANDROID_CONTROL_AE_COMPENSATION_STEP,
    ANDROID_CONTROL_AE_LOCK_AVAILABLE,
    ANDROID_CONTROL_AF_AVAILABLE_MODES,
    ANDROID_CONTROL_AVAILABLE_EFFECTS,
    ANDROID_CONTROL_AVAILABLE_MODES,
    ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
    ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
    ANDROID_CONTROL_AWB_AVAILABLE_MODES,
    ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
    ANDROID_CONTROL_MAX_REGIONS,
    ANDROID_FLASH_INFO_AVAILABLE,
    ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
    ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
    ANDROID_LENS_FACING,
    ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
    ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
    ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
    ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
    ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
    ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
    ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
    ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
    ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
    ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
    ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
    ANDROID_SCALER_CROPPING_TYPE,
    ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
    ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
    ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
    ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
    ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
    ANDROID_SENSOR_ORIENTATION,
    ANDROID_SHADING_AVAILABLE_MODES,
    ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
    ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
    ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
    ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
    ANDROID_SYNC_MAX_LATENCY};
const std::map<RequestTemplate, uint8_t> kTemplateToIntent = {
    {RequestTemplate::PREVIEW, ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW},
    {RequestTemplate::STILL_CAPTURE,
     ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE},
    {RequestTemplate::VIDEO_RECORD,
     ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD},
    {RequestTemplate::VIDEO_SNAPSHOT,
     ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT},
};
}  // namespace

// Constructor sets the default characteristics for vsock camera
VsockCameraMetadata::VsockCameraMetadata(int32_t width, int32_t height,
                                         int32_t fps)
    : width_(width), height_(height), fps_(fps) {
  update(ANDROID_CONTROL_AE_COMPENSATION_RANGE, kControlAeCompensationRange,
         NELEM(kControlAeCompensationRange));
  update(ANDROID_CONTROL_AE_COMPENSATION_STEP, kControlAeCompensationStep,
         NELEM(kControlAeCompensationStep));
  update(ANDROID_CONTROL_AF_AVAILABLE_MODES, kAfAvailableModes,
         NELEM(kAfAvailableModes));
  update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES, &kAvailableSceneMode, 1);
  update(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &kAeLockAvailable, 1);
  update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &kAwbLockAvailable, 1);
  update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
         kScalerAvailableMaxDigitalZoom, NELEM(kScalerAvailableMaxDigitalZoom));
  update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, kAvailableCapabilities,
         NELEM(kAvailableCapabilities));
  update(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &kHardwarelevel, 1);
  update(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
         kAvailableAberrationModes, NELEM(kAvailableAberrationModes));
  update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, &kAntibandingMode, 1);
  update(ANDROID_CONTROL_MAX_REGIONS, kControlMaxRegions,
         NELEM(kControlMaxRegions));
  update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
         &kVideoStabilizationMode, 1);
  update(ANDROID_CONTROL_AWB_AVAILABLE_MODES, &kAwbAvailableMode, 1);
  update(ANDROID_CONTROL_AE_AVAILABLE_MODES, &kAeAvailableMode, 1);
  update(ANDROID_CONTROL_AVAILABLE_EFFECTS, &kAvailableFffect, 1);
  update(ANDROID_CONTROL_AVAILABLE_MODES, kControlAvailableModes,
         NELEM(kControlAvailableModes));
  update(ANDROID_EDGE_AVAILABLE_EDGE_MODES, &kEdgeMode, 1);
  update(ANDROID_FLASH_INFO_AVAILABLE, &kFlashInfo, 1);
  update(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, &kHotPixelMode, 1);
  update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, kJpegAvailableThumbnailSizes,
         NELEM(kJpegAvailableThumbnailSizes));
  update(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
         &kFocusDistanceCalibration, 1);
  update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, &kLensMinFocusDistance, 1);
  update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
         &kOpticalStabilizationMode, 1);
  update(ANDROID_LENS_FACING, &kFacing, 1);
  update(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
         &kNoiseReductionMode, 1);
  update(ANDROID_NOISE_REDUCTION_MODE, &kNoiseReductionMode, 1);
  update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &kPartialResultCount, 1);
  update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &kRequestPipelineMaxDepth, 1);
  update(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, kRequestMaxNumOutputStreams,
         NELEM(kRequestMaxNumOutputStreams));
  update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &kRequestMaxNumInputStreams, 1);
  update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
         kScalerAvailableMaxDigitalZoom, NELEM(kScalerAvailableMaxDigitalZoom));
  update(ANDROID_SCALER_CROPPING_TYPE, &kCroppingType, 1);
  update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, kTestPatternModes,
         NELEM(kTestPatternModes));
  update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &kTimestampSource, 1);
  update(ANDROID_SENSOR_ORIENTATION, &kOrientation, 1);
  update(ANDROID_SHADING_AVAILABLE_MODES, &kAvailableShadingMode, 1);
  update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, &kFaceDetectMode,
         1);
  update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, &kMaxFaceCount, 1);
  update(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
         &kAvailableHotpixelMode, 1);
  update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
         &kLensShadingMapMode, 1);
  update(ANDROID_SYNC_MAX_LATENCY, &kMaxLatency, 1);
  update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, kAvailableRequestKeys,
         NELEM(kAvailableRequestKeys));
  update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, kAvailableResultKeys,
         NELEM(kAvailableResultKeys));
  update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
         kAvailableCharacteristicsKeys, NELEM(kAvailableCharacteristicsKeys));

  // assume max 2bytes/pixel + info because client might provide us pngs
  const int32_t jpeg_max_size = width * height * 2 + sizeof(camera3_jpeg_blob);
  update(ANDROID_JPEG_MAX_SIZE, &jpeg_max_size, 1);

  std::vector<int64_t> min_frame_durations;
  std::vector<int32_t> stream_configurations;
  std::vector<int64_t> stall_durations;

  int64_t frame_duration = 1000000000L / fps;
  for (const auto& format : kHalFormats) {
    stream_configurations.push_back(format);
    min_frame_durations.push_back(format);
    stall_durations.push_back(format);
    stream_configurations.push_back(width);
    min_frame_durations.push_back(width);
    stall_durations.push_back(width);
    stream_configurations.push_back(height);
    min_frame_durations.push_back(height);
    stall_durations.push_back(height);
    stream_configurations.push_back(
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);
    min_frame_durations.push_back(frame_duration);
    stall_durations.push_back((format == HAL_PIXEL_FORMAT_BLOB) ? 2000000000L
                                                                : 0);
  }
  update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
         stream_configurations.data(), stream_configurations.size());
  update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
         min_frame_durations.data(), min_frame_durations.size());
  update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stall_durations.data(),
         stall_durations.size());

  int32_t active_array_size[] = {0, 0, width, height};
  update(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
         active_array_size, NELEM(active_array_size));
  update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, active_array_size,
         NELEM(active_array_size));

  int32_t pixel_array_size[] = {width, height};
  update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixel_array_size,
         NELEM(pixel_array_size));

  int32_t max_frame_rate = fps;
  int32_t min_frame_rate = max_frame_rate / 2;
  int32_t frame_rates[] = {min_frame_rate, max_frame_rate};
  update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, frame_rates,
         NELEM(frame_rates));
  int64_t max_frame_duration = 1000000000L / min_frame_rate;
  update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, &max_frame_duration, 1);
}

VsockCameraRequestMetadata::VsockCameraRequestMetadata(int32_t fps,
                                                       RequestTemplate type) {
  update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &kAberrationMode, 1);
  update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &kExposureCompensation, 1);
  update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &kVideoStabilizationMode, 1);
  update(ANDROID_CONTROL_AWB_MODE, &kAwbAvailableMode, 1);
  update(ANDROID_CONTROL_AE_MODE, &kAeAvailableMode, 1);
  update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &kAePrecaptureTrigger, 1);
  update(ANDROID_CONTROL_AF_MODE, &kAfMode, 1);
  update(ANDROID_CONTROL_AF_TRIGGER, &kAfTrigger, 1);
  update(ANDROID_CONTROL_SCENE_MODE, &kAvailableSceneMode, 1);
  update(ANDROID_CONTROL_EFFECT_MODE, &kAvailableFffect, 1);
  update(ANDROID_FLASH_MODE, &kFlashMode, 1);
  update(ANDROID_JPEG_THUMBNAIL_SIZE, kThumbnailSize, NELEM(kThumbnailSize));
  update(ANDROID_JPEG_QUALITY, &kJpegQuality, 1);
  update(ANDROID_JPEG_THUMBNAIL_QUALITY, &kJpegQuality, 1);
  update(ANDROID_JPEG_ORIENTATION, &kJpegOrientation, 1);
  update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &kOpticalStabilizationMode,
         1);
  update(ANDROID_NOISE_REDUCTION_MODE, &kNoiseReductionMode, 1);
  update(ANDROID_SENSOR_TEST_PATTERN_MODE, &kTestPatternMode, 1);
  update(ANDROID_STATISTICS_FACE_DETECT_MODE, &kFaceDetectMode, 1);
  update(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &kAvailableHotpixelMode, 1);

  int32_t max_frame_rate = fps;
  int32_t min_frame_rate = max_frame_rate / 2;
  int32_t frame_rates[] = {min_frame_rate, max_frame_rate};
  update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, frame_rates, NELEM(frame_rates));

  update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &kAntibandingMode, 1);
  update(ANDROID_CONTROL_MODE, &kControlMode, 1);

  auto it = kTemplateToIntent.find(type);
  if (it != kTemplateToIntent.end()) {
    auto intent = it->second;
    update(ANDROID_CONTROL_CAPTURE_INTENT, &intent, 1);
    is_valid_ = true;
  } else {
    is_valid_ = false;
  }
}

}  // namespace android::hardware::camera::device::V3_4::implementation
