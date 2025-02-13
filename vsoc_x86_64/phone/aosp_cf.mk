#
# Copyright (C) 2019 The Android Open Source Project
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
# All components inherited here go to system image (same as GSI system)
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_system.mk)

PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS := relaxed

#
# All components inherited here go to system_ext image (same as GSI system_ext)
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/handheld_system_ext.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/telephony_system_ext.mk)

#
# All components inherited here go to product image (same as GSI product)
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_product.mk)

#
# All components inherited here go to vendor image
#
$(call inherit-product, device/google/cuttlefish/shared/phone/device_vendor.mk)

# Nested virtualization support
$(call inherit-product, packages/modules/Virtualization/apex/product_packages.mk)

#
# Special settings for the target
#
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64/bootloader.mk)

# Exclude features that are not available on AOSP devices.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/aosp_excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/aosp_excluded_hardware.xml

PRODUCT_NAME := aosp_cf_x86_64_phone
PRODUCT_DEVICE := vsoc_x86_64
PRODUCT_MANUFACTURER := Google
PRODUCT_MODEL := Cuttlefish x86_64 phone

# Window Extensions
$(call inherit-product, $(SRC_TARGET_DIR)/product/window_extensions.mk)

PRODUCT_VENDOR_PROPERTIES += \
    ro.soc.manufacturer=$(PRODUCT_MANUFACTURER) \
    ro.soc.model=$(PRODUCT_DEVICE)

# Ignore all Android.mk files
PRODUCT_IGNORE_ALL_ANDROIDMK := true
# TODO(b/342327756, b/342330305): Allow the following Android.mk files
PRODUCT_ALLOWED_ANDROIDMK_FILES := art/Android.mk art/tools/ahat/Android.mk
# Allow some Android.mk files defined in internal branches
PRODUCT_ANDROIDMK_ALLOWLIST_FILE := vendor/google/build/androidmk/aosp_cf_allowlist.mk

PRODUCT_USE_SOONG_NOTICE_XML := true

# Compare target product name directly to avoid this from any product inherits aosp_cf.mk
ifneq ($(filter aosp_cf_x86_64_phone aosp_cf_x86_64_phone_soong_system aosp_cf_x86_64_foldable,$(TARGET_PRODUCT)),)
# TODO(b/350000347) Enable Soong defined system image from coverage build
ifneq ($(CLANG_COVERAGE),true)
ifneq ($(NATIVE_COVERAGE),true)
USE_SOONG_DEFINED_SYSTEM_IMAGE := true
PRODUCT_SOONG_DEFINED_SYSTEM_IMAGE := aosp_shared_system_image

# For a gradual rollout, we're starting with just enabling this for aosp_cf_x86_64_phone and
# not any of the other products that inherit from it.
ifeq ($(TARGET_PRODUCT),aosp_cf_x86_64_phone)
ifeq (,$(TARGET_BUILD_APPS))
ifeq (,$(UNBUNDLED_BUILD))
PRODUCT_SOONG_ONLY := $(RELEASE_SOONG_ONLY_CUTTLEFISH)
endif
endif
endif

endif # NATIVE_COVERAGE
endif # CLANG_COVERAGE
endif # aosp_cf_x86_64_phone aosp_cf_x86_64_foldable
