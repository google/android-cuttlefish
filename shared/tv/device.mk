#
# Copyright (C) 2017 The Android Open Source Project
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

DEVICE_MANIFEST_FILE += device/google/cuttlefish/shared/config/manifest.xml
DEVICE_MANIFEST_FILE += device/google/cuttlefish/shared/tv/manifest.xml

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_minimal.mk)
$(call inherit-product, device/google/cuttlefish/shared/device.mk)

PRODUCT_PROPERTY_OVERRIDES += \
    ro.lmk.kill_heaviest_task=true \
    ro.lmk.kill_timeout_ms=100 \
    ro.lmk.use_minfree_levels=true \

# HDMI CEC HAL
PRODUCT_PACKAGES += android.hardware.tv.cec@1.0-service.mock

# Tuner HAL
PRODUCT_PACKAGES += android.hardware.tv.tuner@1.0-service

# Enabling managed profiles
PRODUCT_COPY_FILES += frameworks/native/data/etc/android.software.managed_users.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.managed_users.xml
DEVICE_PACKAGE_OVERLAYS += device/google/cuttlefish/shared/tv/overlay
