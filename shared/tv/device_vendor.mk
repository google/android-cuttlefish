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

PRODUCT_MANIFEST_FILES += device/google/cuttlefish/shared/config/product_manifest.xml
SYSTEM_EXT_MANIFEST_FILES += device/google/cuttlefish/shared/config/system_ext_manifest.xml

$(call inherit-product, device/google/atv/products/atv_vendor.mk)

$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
$(call inherit-product, device/google/cuttlefish/shared/graphics/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/swiftshader/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/camera/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/virgl/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/device.mk)

# Extend cuttlefish common sepolicy with tv-specific functionality
BOARD_SEPOLICY_DIRS += device/google/cuttlefish/shared/tv/sepolicy/vendor

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/media_codecs_google_tv.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_tv.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.hdmi.cec.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.hdmi.cec.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.tv.tuner.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.tv.tuner.xml \
    hardware/interfaces/tv/tuner/config/sample_tuner_vts_config_1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/tuner_vts_config_1_0.xml \
    hardware/interfaces/tv/tuner/config/sample_tuner_vts_config_1_1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/tuner_vts_config_1_1.xml \
    hardware/interfaces/tv/tuner/config/sample_tuner_vts_config_aidl_V1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/tuner_vts_config_aidl_V1.xml

# HDMI AIDL HAL
PRODUCT_PACKAGES += \
     android.hardware.tv.hdmi.connection-service

# CEC AIDL HAL
PRODUCT_PACKAGES += \
     android.hardware.tv.hdmi.cec-service

# EARC AIDL HAL
PRODUCT_PACKAGES += \
     android.hardware.tv.hdmi.earc-service

# Setup HDMI CEC as Playback Device
PRODUCT_PROPERTY_OVERRIDES += ro.hdmi.device_type=4

# Tuner HAL
PRODUCT_PACKAGES += android.hardware.tv.tuner-service.example

# Sample Tuner Input for testing
#PRODUCT_PACKAGES += LiveTv sampletunertvinput

# Fallback IME and Home apps
PRODUCT_PACKAGES += LeanbackIME TvSampleLeanbackLauncher

# Enabling managed profiles
DEVICE_PACKAGE_OVERLAYS += device/google/cuttlefish/shared/tv/overlay

TARGET_BOARD_INFO_FILE ?= device/google/cuttlefish/shared/tv/android-info.txt
