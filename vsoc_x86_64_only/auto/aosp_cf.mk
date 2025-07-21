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

# Telephony: Use Minradio RIL instead of Cuttlefish RIL
TARGET_USES_CF_RILD := false
PRODUCT_PACKAGES += com.android.hardware.radio.minradio.virtual
PRODUCT_PACKAGES += ConnectivityOverlayMinradio

# Disable thread network
CF_VENDOR_NO_THREADNETWORK := true

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


LOCAL_USE_VENDOR_AUDIO_CONFIGURATION?= false
ifeq ($(LOCAL_USE_VENDOR_AUDIO_CONFIGURATION),false)
# Auto CF target is configured to use Configurable Audio Policy Engine if vendor audio configuration
# flag is not set.
# However, to prevent fallback on common cuttlefish audio configuration files, make use
# of the vendor flag even for default cuttlefish auto config.
LOCAL_USE_VENDOR_AUDIO_CONFIGURATION := true
$(call inherit-product, device/google/cuttlefish/shared/auto/audio_policy_engine.mk)
endif
#
# Special settings for the target
#
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64/kernel.mk)
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64/bootloader.mk)

# Exclude features that are not available on AOSP devices.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/aosp_excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/aosp_excluded_hardware.xml

PRODUCT_NAME := aosp_cf_x86_64_only_auto
PRODUCT_DEVICE := vsoc_x86_64_only
PRODUCT_MANUFACTURER := Google
PRODUCT_MODEL := Cuttlefish x86_64 auto 64-bit only

PRODUCT_VENDOR_PROPERTIES += \
    ro.soc.manufacturer=$(PRODUCT_MANUFACTURER) \
    ro.soc.model=$(PRODUCT_DEVICE)

ifeq ($(TARGET_PRODUCT),aosp_cf_x86_64_auto)
    PRODUCT_SOONG_ONLY := $(RELEASE_SOONG_ONLY_CUTTLEFISH)
endif