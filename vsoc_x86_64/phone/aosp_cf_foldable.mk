#
# Copyright (C) 2021 The Android Open Source Project
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

# Inherit mostly from aosp_cf_x86_64_phone
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64/phone/aosp_cf.mk)
PRODUCT_NAME := aosp_cf_x86_64_foldable
PRODUCT_MODEL := Cuttlefish x86_64 foldable

# Include the device state configuration for a foldable device.
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/foldable/device_state_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/devicestate/device_state_configuration.xml \
    device/google/cuttlefish/shared/foldable/display_layout_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/displayconfig/display_layout_configuration.xml \
    device/google/cuttlefish/shared/foldable/display_settings.xml:$(TARGET_COPY_OUT_VENDOR)/etc/display_settings.xml \

# Include RRO settings that specify the fold states and screen information.
PRODUCT_PACKAGES += \
    aosp_cuttlefish_foldable_overlay_frameworks_base
# Include the android-info.txt that specifies the foldable --config by default.
TARGET_BOARD_INFO_FILE := device/google/cuttlefish/shared/foldable/android-info.txt
