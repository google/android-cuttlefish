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

PRODUCT_MANIFEST_FILES += device/google/cuttlefish/shared/config/product_manifest.xml
SYSTEM_EXT_MANIFEST_FILES += device/google/cuttlefish/shared/config/system_ext_manifest.xml

$(call inherit-product, $(SRC_TARGET_DIR)/product/handheld_vendor.mk)

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.software.sip.voip.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml

$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
$(call inherit-product, device/google/cuttlefish/shared/biometrics_face/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/biometrics_fingerprint/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/bluetooth/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/consumerir/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/gnss/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/graphics/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/identity/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/reboot_escrow/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/vibrator/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/secure_element/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/swiftshader/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/telephony/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/sensors/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/virgl/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/device.mk)

# Loads the camera HAL and which set of cameras is required.
$(call inherit-product, device/google/cuttlefish/shared/camera/device_vendor.mk)
$(call inherit-product, device/google/cuttlefish/shared/camera/config/standard.mk)

# Support mixing CF system onto previous versions of vendor
PRODUCT_EXTRA_VNDK_VERSIONS := 30 31 32 33 34

TARGET_PRODUCT_PROP := $(LOCAL_PATH)/product.prop
TARGET_VENDOR_PROP := $(LOCAL_PATH)/vendor.prop

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \

    ifneq ($(TARGET_DISABLE_BIOMETRICS_FACE),true)
        PRODUCT_COPY_FILES += \
        frameworks/native/data/etc/android.hardware.biometrics.face.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.biometrics.face.xml \

    endif

DEVICE_PACKAGE_OVERLAYS += device/google/cuttlefish/shared/phone/overlay

# Runtime Resource Overlays
PRODUCT_PACKAGES += cuttlefish_phone_overlay_frameworks_base_core

# NFC AIDL HAL
PRODUCT_PACKAGES += \
    com.google.cf.nfc

TARGET_BOARD_INFO_FILE ?= device/google/cuttlefish/shared/phone/android-info.txt

# Storage: for factory reset protection feature
PRODUCT_VENDOR_PROPERTIES += \
    ro.frp.pst=/dev/block/by-name/frp
