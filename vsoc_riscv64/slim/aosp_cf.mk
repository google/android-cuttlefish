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
# All components inherited here go to system image (same as GSI system)
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
#$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_system.mk)

# TODO: FIXME: Start workaround for generic_system.mk ########################
TARGET_NO_RECOVERY := true
TARGET_FLATTEN_APEX := false

# TODO: this list should come via mainline_system.mk, but for now list
# just the modules that work for riscv64.
PRODUCT_PACKAGES := \
    android.system.suspend-service \
    adbd_system_api \
    apexd \
    bootanim \
    boringssl_self_test \
    cgroups.json \
    com.android.adbd \
    com.android.conscrypt \
    com.android.i18n \
    com.android.runtime \
    fsck.f2fs \
    hwservicemanager \
    init.environ.rc \
    init_first_stage \
    init_system \
    keystore2 \
    libEGL \
    libETC1 \
    libGLESv1_CM \
    libGLESv2 \
    libGLESv3 \
    libpower \
    libpowermanager \
    libvulkan \
    linker \
    logcat \
    logd \
    odsign \
    servicemanager \
    shell_and_utilities \
    surfaceflinger \
    system_compatibility_matrix.xml \
    system_manifest.xml \
    task_profiles.json \
    usbd \
    vdc \
    vold \

PRODUCT_COPY_FILES += \
    system/core/rootdir/init.usb.rc:system/etc/init/hw/init.usb.rc \
    system/core/rootdir/init.usb.configfs.rc:system/etc/init/hw/init.usb.configfs.rc \
    system/core/rootdir/etc/hosts:system/etc/hosts

$(call inherit-product, $(SRC_TARGET_DIR)/product/default_art_config.mk)
PRODUCT_USES_DEFAULT_ART_CONFIG := false

PRODUCT_BRAND := generic
# TODO: FIXME: Stop workaround for generic_system.mk #########################

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
LOCAL_PREFER_VENDOR_APEX := true
#$(call inherit-product, device/google/cuttlefish/shared/slim/device_vendor.mk)

# TODO: FIXME: Start workaround for slim/device_vendor.mk ####################
PRODUCT_MANIFEST_FILES += device/google/cuttlefish/shared/config/product_manifest.xml
SYSTEM_EXT_MANIFEST_FILES += device/google/cuttlefish/shared/config/system_ext_manifest.xml

$(call inherit-product, $(SRC_TARGET_DIR)/product/handheld_vendor.mk)

$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
$(call inherit-product, device/google/cuttlefish/shared/camera/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/graphics/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/telephony/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/virgl/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/device.mk)

PRODUCT_VENDOR_PROPERTIES += \
    debug.hwui.drawing_enabled=0 \

PRODUCT_PACKAGES += \
    com.google.aosp_cf_phone.rros \
    com.google.aosp_cf_slim.rros

TARGET_BOARD_INFO_FILE ?= device/google/cuttlefish/shared/slim/android-info.txt
# TODO: FIXME: Stop workaround for slim/device_vendor.mk #####################

# TODO(b/205788876) remove this when openwrt has an image for riscv64
#PRODUCT_ENFORCE_MAC80211_HWSIM := false

#
# Special settings for the target
#
$(call inherit-product, device/google/cuttlefish/vsoc_riscv64/bootloader.mk)

# Exclude features that are not available on AOSP devices.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/aosp_excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/aosp_excluded_hardware.xml

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

PRODUCT_NAME := aosp_cf_riscv64_slim
PRODUCT_DEVICE := vsoc_riscv64
PRODUCT_MANUFACTURER := Google
PRODUCT_MODEL := Cuttlefish riscv64 slim

PRODUCT_VENDOR_PROPERTIES += \
    ro.soc.manufacturer=$(PRODUCT_MANUFACTURER) \
    ro.soc.model=$(PRODUCT_DEVICE)
