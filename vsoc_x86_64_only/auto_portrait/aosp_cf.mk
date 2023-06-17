#
# Copyright (C) 2023 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# AOSP Car UI Portrait Cuttlefish Target

TARGET_BOARD_INFO_FILE := device/google/cuttlefish/shared/auto_portrait/android-info.txt

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/auto_portrait/display_settings.xml:$(TARGET_COPY_OUT_VENDOR)/etc/display_settings.xml

# Exclude AAE Car System UI
DO_NOT_INCLUDE_AAE_CAR_SYSTEM_UI := true

# Exclude Car UI Reference Design
DO_NOT_INCLUDE_CAR_UI_REFERENCE_DESIGN := true

# Exclude Car Visual Overlay
DISABLE_CAR_PRODUCT_VISUAL_OVERLAY := true

# Copy additional files
PRODUCT_COPY_FILES += \
    packages/services/Car/car_product/car_ui_portrait/bootanimation/bootanimation.zip:system/media/bootanimation.zip

$(call inherit-product, device/google/cuttlefish/vsoc_x86_64_only/auto/aosp_cf.mk)

PRODUCT_NAME := aosp_cf_x86_64_only_auto_portrait
PRODUCT_DEVICE := vsoc_x86_64_only
PRODUCT_MANUFACTURER := Google
PRODUCT_MODEL := AOSP Cuttlefish x86_64 auto 64-bit only with portrait UI

$(call inherit-product, packages/services/Car/car_product/car_ui_portrait/apps/car_ui_portrait_apps.mk)
$(call inherit-product, packages/services/Car/car_product/car_ui_portrait/rro/car_ui_portrait_rro.mk)

PRODUCT_COPY_FILES += \
    packages/services/Car/car_product/car_ui_portrait/car_ui_portrait_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/car_ui_portrait_hardware.xml

# Include the`launch_cvd --config auto_portrait` option.
$(call soong_config_append,cvd,launch_configs,cvd_config_auto_portrait.json)