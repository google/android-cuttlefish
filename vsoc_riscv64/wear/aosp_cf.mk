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

#
# All components inherited here go to system image
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, device/google/cuttlefish/shared/wear/aosp_system.mk)

# TODO: FIXME: Start workaround for aosp_system.mk ########################

# TODO(b/271573990): It is currently required that dexpreopt be enabled for
# userdebug builds, but dexpreopt is not yet supported for this architecture.
# In the interim, this flag allows us to indicate that we cannot run dex2oat
# to build the ART boot image. Once the requirement is relaxed or support
# is enabled for this architecture, this flag can be removed.
PRODUCT_USES_DEFAULT_ART_CONFIG := false

# TODO(b/275113769): The riscv64 architecture doesn't support APEX flattening yet.
# This condition can be removed after support is enabled.
OVERRIDE_TARGET_FLATTEN_APEX := false

# TODO: FIXME: Stop workaround for aosp_system.mk #########################

# Cuttlefish uses A/B with system_b preopt, so we must install these
PRODUCT_PACKAGES += \
    cppreopts.sh \
    otapreopt_script \

# Hacks to boot with basic AOSP system apps
PRODUCT_PACKAGES += \
    Contacts \
    Launcher3QuickStep \
    Provision \
    Settings \
    StorageManager \
    SystemUI \

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.software.app_widgets.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.app_widgets.xml \

#
# All components inherited here go to system_ext image
#
$(call inherit-product, device/google/cuttlefish/shared/wear/aosp_system_ext.mk)

#
# All components inherited here go to product image
#
$(call inherit-product, device/google/cuttlefish/shared/wear/aosp_product.mk)

#
# All components inherited here go to vendor image
#
$(call inherit-product, device/google/cuttlefish/shared/wear/aosp_vendor.mk)
#$(call inherit-product, device/google/cuttlefish/shared/wear/device_vendor.mk)

# TODO: FIXME: Start workaround for wear/device_vendor.mk ####################
PRODUCT_MANIFEST_FILES += device/google/cuttlefish/shared/config/product_manifest.xml
SYSTEM_EXT_MANIFEST_FILES += device/google/cuttlefish/shared/config/system_ext_manifest.xml

$(call inherit-product, $(SRC_TARGET_DIR)/product/handheld_vendor.mk)

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.software.backup.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.backup.xml \
    frameworks/native/data/etc/android.software.connectionservice.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.connectionservice.xml \
    frameworks/native/data/etc/android.software.device_admin.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.device_admin.xml \
    frameworks/native/data/etc/wearable_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/wearable_core_hardware.xml \

$(call inherit-product, device/google/cuttlefish/shared/graphics/device_vendor.mk)
# TODO: FIXME: Enable swiftshader for graphics.
#$(call inherit-product, device/google/cuttlefish/shared/swiftshader/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/telephony/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/virgl/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/device.mk)

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.audio.output.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.output.xml \
    frameworks/native/data/etc/android.hardware.faketouch.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.faketouch.xml \
    frameworks/native/data/etc/android.hardware.location.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.xml \

# Runtime Resource Overlays
PRODUCT_PACKAGES += \
    cuttlefish_phone_overlay_frameworks_base_core \
    cuttlefish_wear_overlay_frameworks_base_core \
    cuttlefish_wear_overlay_settings_provider \

PRODUCT_PRODUCT_PROPERTIES += \
    config.disable_cameraservice=true

PRODUCT_CHARACTERISTICS := nosdcard,watch

TARGET_BOARD_INFO_FILE ?= device/google/cuttlefish/shared/wear/android-info.txt
# TODO: FIXME: Stop workaround for wear/device_vendor.mk #####################

#
# Special settings for the target
#
$(call inherit-product, device/google/cuttlefish/vsoc_riscv64/bootloader.mk)

# Exclude features that are not available on AOSP devices.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/aosp_excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/aosp_excluded_hardware.xml \

# TODO(b/206676167): This property can be removed when renderscript is removed.
# Prevents framework from attempting to load renderscript libraries, which are
# not supported on this architecture.
PRODUCT_SYSTEM_PROPERTIES += \
    config.disable_renderscript=1 \

# TODO(b/271573990): This property can be removed when ART support for JIT on
# this architecture is available. This is an override as the original property
# is defined in runtime_libart.mk.
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.usejit=false

PRODUCT_NAME := aosp_cf_riscv64_wear
PRODUCT_DEVICE := vsoc_riscv64
PRODUCT_MANUFACTURER := Google
PRODUCT_MODEL := Cuttlefish riscv64 wearable

PRODUCT_VENDOR_PROPERTIES += \
    ro.soc.manufacturer=$(PRODUCT_MANUFACTURER) \
    ro.soc.model=$(PRODUCT_DEVICE)
