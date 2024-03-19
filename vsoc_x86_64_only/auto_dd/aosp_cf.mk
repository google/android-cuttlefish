#
# Copyright (C) 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# AOSP Car UI Distant Display Cuttlefish Target
TARGET_BOARD_INFO_FILE := device/google/cuttlefish/shared/auto_dd/android-info.txt

# Exclude AAE Car System UI
DO_NOT_INCLUDE_AAE_CAR_SYSTEM_UI := true

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/auto_dd/display_settings.xml:$(TARGET_COPY_OUT_VENDOR)/etc/display_settings.xml

PRODUCT_PACKAGE_OVERLAYS += \
    device/google/cuttlefish/shared/auto_dd/overlay

EMULATOR_DYNAMIC_MULTIDISPLAY_CONFIG := false
BUILD_EMULATOR_CLUSTER_DISPLAY := true
TARGET_NO_TELEPHONY := true

PRODUCT_SYSTEM_PROPERTIES += \
    ro.emulator.car.distantdisplay=true

$(call inherit-product, packages/services/Car/car_product/distant_display/apps/car_ui_distant_display_apps.mk)
$(call inherit-product, packages/services/Car/car_product/distant_display/rro/distant_display_rro.mk)

# Enable per-display power management
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/auto_dd/display_layout_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/displayconfig/display_layout_configuration.xml

# Disable shared system image checking
PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS := false

$(call inherit-product, device/google/cuttlefish/vsoc_x86_64_only/auto/aosp_cf.mk)

PRODUCT_NAME := aosp_cf_x86_64_auto_dd
PRODUCT_MODEL := Cuttlefish x86_64 auto 64-bit only distant displays

# Include the`launch_cvd --config auto_dd` option.
$(call soong_config_append,cvd,launch_configs,cvd_config_auto_dd.json)