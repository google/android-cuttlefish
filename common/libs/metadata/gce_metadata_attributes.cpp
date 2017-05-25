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
#include "common/libs/metadata/gce_metadata_attributes.h"

const char* const GceMetadataAttributes::kTestingPath = "testing/attributes/";

const char* const GceMetadataAttributes::kInstancePath = "instance/attributes/";

const char* const GceMetadataAttributes::kProjectPath = "project/attributes/";

// Version of Android to boot on multiboot images.
// Updates at boot time.
// Example: 23_gce_x86_64_phone
// When in doubt leave blank or use default
const char* const GceMetadataAttributes::kAndroidVersionKey = "android_version";

// Configuration of the back camera.
// *** OBSOLETE ***
// Updates at boot time.
// Example: 1,768,1024,checker-sliding
// NOTE: To escape the commas with gcloud use
//   --metadata-from-file camera_back=<(echo 1,768,1024,checker-sliding)
const char* const GceMetadataAttributes::kBackCameraConfigKey = "camera_back";

// Content of (not a path to) an extra init file
// Updates at boot time.
const char* const GceMetadataAttributes::kCustomInitFileKey =
    "custom_init_file";

// Configuration of the display
// *** OBSOLETE ***
// Updates at boot time.
// Example: 800x1280x16x213
// Format is ${x_res}x${y_res}x${depth}x${dpi}
// Note: depth is currently ignored. 32 is the forward-compatible value.
const char* const GceMetadataAttributes::kDisplayConfigurationKey =
    "cfg_sta_display_resolution";

// Configuration of the front camera.
// Updates at boot time.
// Example: 1,768,1024,checker-sliding
// Note: To escape the commas with gcloud use
//   --metadata-from-file camera_front=<(echo 1,768,1024,checker-sliding)
const char* const GceMetadataAttributes::kFrontCameraConfigKey = "camera_front";

// Current GPS location.
// Updates at runtime.
// Example: -122.0840859,37.4224504,0,0,0,10
// The fields are:
//   Logitude in degress with decimal subdegrees
//   Latitude in degress with decimal subdegrees
//   Altitude in meters
//   Heading in degress
//   Speed in meters per second
//   Precision in meters
const char* const GceMetadataAttributes::kGpsCoordinatesKey = "gps_coordinates";

// Initial locale of the device.
// Updates at boot time.
// Example: en_US
// If this attribute is set any existing locale is forgotten and the new
// one is set.
// If no value is set then the existing locale, if any, remains.
const char* const GceMetadataAttributes::kInitialLocaleKey =
    "cfg_sta_initial_locale";

// State of Wireless LAN.
// Updates at runtime.
// Example: ENABLED
// Accepted values are: ( ENABLED, DISABLED ). If value is neither of these, the
// handler falls back to default state, DISABLED.
const char* const GceMetadataAttributes::kWlanStateKey = "cfg_sta_wlan_state";

// Configuration of the physical keyboard (if any).
// Updates at boot time.
// If the attribute is not specified will enable physical keyboard.
// Values accepted:
//   1: Use a physical keyboard.
//   All other values: disable the physical keyboard.
const char* const GceMetadataAttributes::kPhysicalKeyboardKey =
    "physical_keyboard";

// Configuration of hardware buttons.
// *** OBSOLETE ***
// Updates at boot time.
// If the attribute is not specified will enable all hardware buttons.
// Values accepted:
//   Empty string: disable all hardware buttons.
//   MENU: enable only the menu button.
//   BACK: enable only the back button.
//   HOME: enable only the home button.
//   Any combinion of the above values delimited by commas.
const char* const GceMetadataAttributes::kPhysicalNavigationButtonsKey =
    "physical_navigation_buttons";

// Path to the device node to mount as a system overlay.
// Updates at boot time.
// If absent no overlay is mounted.
// Example: /dev/block/sdb1
const char* const GceMetadataAttributes::kSystemOverlayDeviceKey =
    "system_overlay_device";

// Device personality definition in JSON format.
// Allows custom device features to be set.
// If absent - not used.
const char* const GceMetadataAttributes::kDevicePersonalityDefinitionKey =
    "device_personality_definition";

// Device personality name.
// Holds name (without path or extension) of the file describing device
// features. File describing features should be located in
// GceResourceLocation::kDevicePersonalityPath.
// If empty, not present, or invalid falls back to 'default'.
const char* const GceMetadataAttributes::kDevicePersonalityNameKey =
    "device_personality_name";

// Controls which streaming protocol will be used by the remoter.
// Values include:
//   appstreaming
//   vnc
// If nothing is set the remoter behaves as if vnc is selected.
const char* const GceMetadataAttributes::kRemotingModeKey =
    "cfg_sta_remoting_mode";

// Controls the scaling used in the VNC backend. The the device is
// configured to 320 dpi but this is set to 160, the result will be
// as if a scaling factor of 0.5 were used.
const char* const GceMetadataAttributes::kConsoleDpiScalingDpiKey =
    "cfg_sta_console_scaling_dpi";

// Controls how the pixels will be scaled before they are sent over VNC.
// Numbers are floating point and must be <= 1.0 If nothing is set then
// 1.0 is assumed.
// Note: VNC doesn't cope well with unusual scaling factors: it tends to
// clip the right / bottom off the screen. 0.5 and 0.25 appear to be safe.
// If nothing is set then 1.0 is assumed.
const char* const GceMetadataAttributes::kConsoleScalingFactorKey =
    "cfg_sta_console_scaling_factor";

// Controls whether metadata attribute injections are permitted from
// within Android.
// The only legitimate purpose of this attribute is testing.
// Not all attributes are permitted.
const char* const GceMetadataAttributes::kTestAllowMetadataInjectionsKey =
  "cfg_test_allow_metadata_injections";

// Controls which RIL implementation will be used during next boot.
// Values include:
// - TESTING = use testing version of GCE RIL (if available).
// - DEPRECATED = use previous version of GCE RIL (if available).
// - DEFAULT / unset / other  = use current version of GCE RIL.
const char* const GceMetadataAttributes::kRilVersionKey = "ril_version";

// Controls which Hardware composer implementation will be used during next
// boot.
// Values include:
// - TESTING = use testing version of GCE Hardware Composer (if available).
// - DEPRECATED = use previous version of GCE Hardware Composer (if available).
// - DEFAULT / unset / other  = use current version of GCE Hardware Composer.
const char* const GceMetadataAttributes::kHWComposerVersionKey = "hwc_version";

// Controls which VNC implementation will be used during next boot
// Values include:
// - TESTING = use testing version of the GCE VNC server (if available)
// - DEPRECATED = use previous version of the GCE VNC server (if available).
// - DEFAULT / unset / other  = use current version of the GCE VNC server
const char* const GceMetadataAttributes::kVncServerVersionKey =
    "vnc_server_version";

const char* const GceMetadataAttributes::kSshKeysInstancePath =
    "instance/attributes/sshKeys";
const char* const GceMetadataAttributes::kSshKeysProjectPath =
    "project/attributes/sshKeys";

const char* const GceMetadataAttributes::kInjectedIntentInstancePath =
    "instance/attributes/t_force_intent";
const char* const GceMetadataAttributes::kInjectedIntentProjectPath =
    "project/attributes/t_force_intent";

const char* const GceMetadataAttributes::kForceCoarseOrientationChangeInstancePath =
    "instance/attributes/t_force_orientation";
const char* const GceMetadataAttributes::kForceCoarseOrientationChangeProjectPath =
    "project/attributes/t_force_orientation";
const char* const GceMetadataAttributes::kForceCoarseOrientationChangeTestingPath =
    "testing/attributes/t_force_orientation";

const char* const GceMetadataAttributes::kPropertyMapperInstancePath =
    "instance/attributes/cfg_sta_pmap_";
const char* const GceMetadataAttributes::kPropertyMapperProjectPath =
    "project/attributes/cfg_sta_pmap_";

const char* const GceMetadataAttributes::kAccelerometerSensorInstancePath =
    "instance/attributes/t_sensor_accelerometer";
const char* const GceMetadataAttributes::kAccelerometerSensorProjectPath =
    "project/attributes/t_sensor_accelerometer";

const char* const GceMetadataAttributes::kGyroscopeSensorInstancePath =
    "instance/attributes/t_sensor_gyroscope";
const char* const GceMetadataAttributes::kGyroscopeSensorProjectPath =
    "project/attributes/t_sensor_gyroscope";

const char* const GceMetadataAttributes::kLightSensorInstancePath =
    "instance/attributes/t_sensor_light";
const char* const GceMetadataAttributes::kLightSensorProjectPath =
    "project/attributes/t_sensor_light";

const char* const GceMetadataAttributes::kMagneticFieldSensorInstancePath =
    "instance/attributes/t_sensor_magnetic_field";
const char* const GceMetadataAttributes::kMagneticFieldSensorProjectPath =
    "project/attributes/t_sensor_magnetic_field";

const char* const GceMetadataAttributes::kPressureSensorInstancePath =
    "instance/attributes/t_sensor_pressure";
const char* const GceMetadataAttributes::kPressureSensorProjectPath =
    "project/attributes/t_sensor_pressure";

const char* const GceMetadataAttributes::kProximitySensorInstancePath =
    "instance/attributes/t_sensor_proximity";
const char* const GceMetadataAttributes::kProximitySensorProjectPath =
    "project/attributes/t_sensor_proximity";

const char* const GceMetadataAttributes::kAmbientTemperatureSensorInstancePath =
    "instance/attributes/t_sensor_ambient_temperature";
const char* const GceMetadataAttributes::kAmbientTemperatureSensorProjectPath =
    "project/attributes/t_sensor_ambient_temperature";

const char* const GceMetadataAttributes::kDeviceTemperatureSensorInstancePath =
    "instance/attributes/t_sensor_device_temperature";
const char* const GceMetadataAttributes::kDeviceTemperatureSensorProjectPath =
    "project/attributes/t_sensor_device_temperature";

const char* const GceMetadataAttributes::kRelativeHumiditySensorInstancePath =
    "instance/attributes/t_sensor_relative_humidity";
const char* const GceMetadataAttributes::kRelativeHumiditySensorProjectPath =
    "project/attributes/t_sensor_relative_humidity";
const char* const GceMetadataAttributes::kAutoScreenshotFrequencyInstancePath =
    "instance/attributes/t_auto_screenshot_frequency";
const char* const GceMetadataAttributes::kAutoScreenshotPrefixInstancePath =
    "instance/attributes/t_auto_screenshot_prefix";
const char* const GceMetadataAttributes::kRebootIfMissingInstancePath =
    "instance/attributes/t_reboot_if_missing_path";

const char* const GceMetadataAttributes::kPowerBatteryConfigPath =
    "instance/attributes/power_battery_config";

const char* const GceMetadataAttributes::kMobileDataNetworkingConfigPath =
    "instance/attributes/mobile_data_networking_config";
// IMEI value which must be set before the system starts to boot. If not,
// a randomly generated value will be used by the GCE RIL module.
const char* const GceMetadataAttributes::kIMEIConfigKey = "imei";

const char* const GceMetadataAttributes::kScreenshotsDirectoryInstancePath =
    "instance/attributes/t_screenshot_dir_path";
const char* const GceMetadataAttributes::kScreenshotsDirectoryProjectPath =
    "project/attributes/t_screenshot_dir_path";
