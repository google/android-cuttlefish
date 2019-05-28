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
# All components here go to product image (same as GSI product)
#

# GSI includes all AOSP product packages and placed under /system/product
$(call inherit-product, $(SRC_TARGET_DIR)/product/handheld_product.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/telephony_product.mk)

# Default AOSP sounds
$(call inherit-product-if-exists, frameworks/base/data/sounds/AllAudio.mk)

# TODO(b/133643923): Clean up the mainline whitelist
PRODUCT_ARTIFACT_PATH_REQUIREMENT_WHITELIST += \
    system/app/messaging/messaging.apk \
    system/app/WAPPushManager/WAPPushManager.apk \
    system/bin/healthd \
    system/etc/init/healthd.rc \
    system/etc/seccomp_policy/crash_dump.%.policy \
    system/etc/seccomp_policy/mediacodec.policy \
    system/etc/vintf/manifest/manifest_healthd.xml \
    system/lib/libframesequence.so \
    system/lib/libgiftranscode.so \
    system/lib64/libframesequence.so \
    system/lib64/libgiftranscode.so \

# Some GSI builds enable dexpreopt, whitelist these preopt files
PRODUCT_ARTIFACT_PATH_REQUIREMENT_WHITELIST += %.odex %.vdex %.art

# Additional settings used in all AOSP builds
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.ringtone=Ring_Synth_04.ogg \
    ro.com.android.dataroaming=true \

# Default AOSP packages
PRODUCT_PACKAGES += \
    messaging \
    PhotoTable \
    WAPPushManager \
    WallpaperPicker \

# Telephony:
#   Provide a APN configuration to GSI product
PRODUCT_COPY_FILES += \
    device/sample/etc/apns-full-conf.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/apns-conf.xml

# NFC:
#   Provide a libnfc-nci.conf to GSI product
PRODUCT_COPY_FILES += \
    device/generic/common/nfc/libnfc-nci.conf:$(TARGET_COPY_OUT_PRODUCT)/etc/libnfc-nci.conf
