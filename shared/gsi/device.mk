#
# Copyright (C) 2018 The Android Open Source Project
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

$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base_telephony.mk)
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)

# use include below so PRODUCT_SHIPPING_API_LEVEL can be overriden
include device/google/cuttlefish/shared/device.mk
PRODUCT_SHIPPING_API_LEVEL := 27

CUTTLEFISH_SYSTEM_AS_ROOT := true

PRODUCT_CHARACTERISTICS := nosdcard

PRODUCT_PROPERTY_OVERRIDES += \
    keyguard.no_require_sim=true \
    rild.libpath=libvsoc-ril.so \
    ro.cdma.home.operator.alpha=Android \
    ro.cdma.home.operator.numeric=302780 \
    ro.gsm.home.operator.alpha=Android \
    ro.gsm.home.operator.numeric=302780 \
    gsm.sim.operator.numeric=302780 \
    gsm.sim.operator.alpha=Android \
    gsm.sim.operator.iso-country=us

PRODUCT_PACKAGES += \
    MmsService \
    Phone \
    PhoneService \
    Telecom \
    TeleService \
    libvsoc-ril \
    rild \

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml

# libGLES_android, SdkSetup, and vintf are in current AOSP GSI
# SdkSetup may need to be removed when it's moved to emulator vendor.
PRODUCT_PACKAGES += \
    libGLES_android \
    SdkSetup \
    vintf

# Needed for /system/priv-app/SdkSetup/SdkSetup.apk to pass CTS android.permission2.cts.PrivappPermissionsTest.
PRODUCT_COPY_FILES += \
    device/generic/goldfish/data/etc/permissions/privapp-permissions-goldfish.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/privapp-permissions-goldfish.xml

# NFC:
#   Provide default libnfc-nci.conf file for devices that does not have one in
#   vendor/etc because aosp system image (of aosp_$arch products) is going to
#   be used as GSI.
#   May need to remove the following for newly launched devices in P since this
#   NFC configuration file should be in vendor/etc, instead of system/etc
PRODUCT_COPY_FILES += \
    device/generic/common/nfc/libnfc-nci.conf:system/etc/libnfc-nci.conf

# These flags are important for the GSI, but break auto
PRODUCT_ENFORCE_RRO_TARGETS := framework-res
PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS := device/google/cuttlefish/shared/overlay
