/*
 * Copyright (C) 2016 The Android Open Source Project
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
#ifndef COMMON_METADATA_GCE_METADATA_ATTRIBUTES_H_
#define COMMON_METADATA_GCE_METADATA_ATTRIBUTES_H_

// Constant string declarations for GCE metadata attributes used by the Remoter.
class GceMetadataAttributes {
public:
  static const char* const kTestingPath;
  static const char* const kInstancePath;
  static const char* const kProjectPath;

  // The InitialMetadataReader reasons in terms of keys, while the
  // MetadataService uses full paths.
  // A key represents two paths, one for the instance and one for the project.
  // When both paths are present the instance value takes precedence.
  //
  // TODO(ghartman): Refactor the MetadataService to use keys as well.
  static const char* const kAndroidVersionKey;
  static const char* const kBackCameraConfigKey;
  static const char* const kConsoleDpiScalingDpiKey;
  static const char* const kConsoleScalingFactorKey;
  static const char* const kCustomInitFileKey;
  static const char* const kDevicePersonalityDefinitionKey;
  static const char* const kDevicePersonalityNameKey;
  static const char* const kDisplayConfigurationKey;
  static const char* const kFrontCameraConfigKey;
  static const char* const kGpsCoordinatesKey;
  static const char* const kInitialLocaleKey;
  static const char* const kWlanStateKey;
  static const char* const kPhysicalKeyboardKey;
  static const char* const kPhysicalNavigationButtonsKey;
  static const char* const kRemotingModeKey;
  static const char* const kSystemOverlayDeviceKey;
  static const char* const kRilVersionKey;
  static const char* const kHWComposerVersionKey;
  static const char* const kVncServerVersionKey;

  static const char* const kTestAllowMetadataInjectionsKey;

  static const char* const kSshKeysInstancePath;
  static const char* const kSshKeysProjectPath;
  static const char* const kInjectedIntentInstancePath;
  static const char* const kInjectedIntentProjectPath;
  static const char* const kForceCoarseOrientationChangeTestingPath;
  static const char* const kForceCoarseOrientationChangeInstancePath;
  static const char* const kForceCoarseOrientationChangeProjectPath;
  static const char* const kPropertyMapperInstancePath;
  static const char* const kPropertyMapperProjectPath;

  static const char* const kAccelerometerSensorInstancePath;
  static const char* const kAccelerometerSensorProjectPath;
  static const char* const kGyroscopeSensorInstancePath;
  static const char* const kGyroscopeSensorProjectPath;
  static const char* const kLightSensorInstancePath;
  static const char* const kLightSensorProjectPath;
  static const char* const kMagneticFieldSensorInstancePath;
  static const char* const kMagneticFieldSensorProjectPath;
  static const char* const kPressureSensorInstancePath;
  static const char* const kPressureSensorProjectPath;
  static const char* const kProximitySensorInstancePath;
  static const char* const kProximitySensorProjectPath;
  static const char* const kDeviceTemperatureSensorInstancePath;
  static const char* const kDeviceTemperatureSensorProjectPath;
  static const char* const kAmbientTemperatureSensorInstancePath;
  static const char* const kAmbientTemperatureSensorProjectPath;
  static const char* const kRelativeHumiditySensorInstancePath;
  static const char* const kRelativeHumiditySensorProjectPath;
  static const char* const kAutoScreenshotFrequencyInstancePath;
  static const char* const kAutoScreenshotPrefixInstancePath;
  static const char* const kRebootIfMissingInstancePath;

  static const char* const kPowerBatteryConfigPath;

  static const char* const kMobileDataNetworkingConfigPath;
  static const char* const kIMEIConfigKey;

  static const char* const kScreenshotsDirectoryInstancePath;
  static const char* const kScreenshotsDirectoryProjectPath;
};

#endif  // COMMON_METADATA_GCE_METADATA_ATTRIBUTES_H_
