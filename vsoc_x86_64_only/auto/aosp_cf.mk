#
# Copyright (C) 2022 The Android Open Source Project
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

#
# All components inherited here go to system image
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, packages/services/Car/car_product/build/car_generic_system.mk)

# FIXME: generic_system.mk sets 'PRODUCT_ENFORCE_RRO_TARGETS := *'
#        but this breaks phone_car. So undo it here.
PRODUCT_ENFORCE_RRO_TARGETS := frameworks-res

PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS := true

# HSUM is currently incompatible with telephony.
# TODO(b/283853205): Properly disable telephony using per-partition makefile.
TARGET_NO_TELEPHONY := true

#
# All components inherited here go to system_ext image
#
$(call inherit-product, packages/services/Car/car_product/build/car_system_ext.mk)

#
# All components inherited here go to product image
#
$(call inherit-product, packages/services/Car/car_product/build/car_product.mk)

#
# All components inherited here go to vendor image
#
$(call inherit-product, device/google/cuttlefish/shared/auto/device_vendor.mk)

#
# Special settings for the target
#
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64/kernel.mk)
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64/bootloader.mk)

# Exclude features that are not available on AOSP devices.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/aosp_excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/aosp_excluded_hardware.xml

# Exclude features that are not available on automotive cuttlefish devices.
# TODO(b/351896700): Remove this workaround once support for uncalibrated accelerometer and
# uncalibrated gyroscope are added to automotive cuttlefish.
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/vsoc_x86_64_only/auto/exclude_unavailable_imu_features.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/exclude_unavailable_imu_features.xml

PRODUCT_NAME := aosp_cf_x86_64_only_auto
PRODUCT_DEVICE := vsoc_x86_64_only
PRODUCT_MANUFACTURER := Google
PRODUCT_MODEL := Cuttlefish x86_64 auto 64-bit only

PRODUCT_VENDOR_PROPERTIES += \
    ro.soc.manufacturer=$(PRODUCT_MANUFACTURER) \
    ro.soc.model=$(PRODUCT_DEVICE)
