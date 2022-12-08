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

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_ramdisk.mk)

PRODUCT_COMPRESSED_APEX := false
$(call inherit-product, $(SRC_TARGET_DIR)/product/updatable_apex.mk)

$(call soong_config_append,cvd,launch_configs,cvd_config_minidroid.json)

PRODUCT_SYSTEM_PROPERTIES += \
    service.adb.listen_addrs=vsock:5555 \
    apexd.payload_metadata.path=/dev/block/by-name/payload-metadata

VENDOR_SECURITY_PATCH := $(PLATFORM_SECURITY_PATCH)
BOOT_SECURITY_PATCH := $(PLATFORM_SECURITY_PATCH)
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.boot_security_patch=$(BOOT_SECURITY_PATCH)

# Disable Treble and the VNDK
PRODUCT_FULL_TREBLE_OVERRIDE := false
PRODUCT_USE_VNDK_OVERRIDE := false
PRODUCT_USE_PRODUCT_VNDK_OVERRIDE := false

PRODUCT_SHIPPING_API_LEVEL := 33

PRODUCT_USE_DYNAMIC_PARTITIONS := true

PRODUCT_BUILD_VENDOR_IMAGE := true
TARGET_COPY_OUT_VENDOR := vendor

PRODUCT_BRAND := generic

# Stolen from microdroid/Android.bp
PRODUCT_PACKAGES += \
    init_second_stage \
    libbinder \
    libbinder_ndk \
    libstdc++ \
    secilc \
    libadbd_auth \
    libadbd_fs \
    heapprofd_client_api \
    libartpalette-system \
    apexd \
    atrace \
    debuggerd \
    linker \
    linkerconfig \
    servicemanager \
    tombstoned \
    tombstone_transmit.microdroid \
    cgroups.json \
    task_profiles.json \
    public.libraries.android.txt \

# Shell and utilities
PRODUCT_PACKAGES += \
    reboot \
    sh \
    strace \
    toolbox \
    toybox \

# Additional packages
PRODUCT_PACKAGES += \
    com.android.runtime \
    com.android.adbd \
    mdnsd \

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/minidroid/fstab.minidroid:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.minidroid \
    device/google/cuttlefish/shared/minidroid/fstab.minidroid:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.minidroid \

# FIXME: Hack to get some rootdirs created
PRODUCT_PACKAGES += \
    init.environ.rc

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/minidroid/init.rc:system/etc/init/hw/init.minidroid.rc \
    packages/modules/Virtualization/microdroid/ueventd.rc:vendor/etc/ueventd.rc \

DEVICE_MANIFEST_FILE := \
    packages/modules/Virtualization/microdroid/microdroid_vendor_manifest.xml
PRODUCT_PACKAGES += vendor_compatibility_matrix.xml

TARGET_BOARD_INFO_FILE ?= device/google/cuttlefish/shared/minidroid/android-info.txt
