#
# Copyright (C) 2020 The Android Open Source Project
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
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false

#
# FIXME: Set up Go defaults because we are currently limited (by a U-Boot bug)
#        to 512MB of RAM
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/go_defaults_512.mk)
PRODUCT_ARTIFACT_PATH_REQUIREMENT_ALLOWED_LIST += \
    system/apex/com.android.tethering.inprocess.apex \
    system/apex/com.android.tethering.inprocess.capex \
    system/app/PlatformCaptivePortalLogin/PlatformCaptivePortalLogin.apk \
    system/etc/permissions/platform_privapp_allowlist_com.android.cellbroadcastservice.xml \
    system/priv-app/CellBroadcastServiceModulePlatform/CellBroadcastServiceModulePlatform.apk \
    system/priv-app/InProcessNetworkStack/InProcessNetworkStack.apk \

#
# All components inherited here go to vendor image
#
$(call inherit-product, device/google/cuttlefish/shared/phone/device_vendor.mk)

# TODO(b/205788876) remove this when openwrt has an image for arm.
PRODUCT_ENFORCE_MAC80211_HWSIM := false

#
# Special settings for the target
#
$(call inherit-product, device/google/cuttlefish/vsoc_arm_only/bootloader.mk)

# Exclude features that are not available on AOSP devices.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/aosp_excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/aosp_excluded_hardware.xml

PRODUCT_NAME := aosp_cf_arm_only_phone
PRODUCT_DEVICE := vsoc_arm_only
PRODUCT_MANUFACTURER := Google
PRODUCT_MODEL := Cuttlefish arm phone 32-bit only

PRODUCT_VENDOR_PROPERTIES += \
    ro.config.low_ram=true \
    ro.soc.manufacturer=$(PRODUCT_MANUFACTURER) \
    ro.soc.model=$(PRODUCT_DEVICE)

TARGET_SYSTEM_PROP += \
    build/make/target/board/go_defaults_512.prop \
    build/make/target/board/go_defaults_common.prop
