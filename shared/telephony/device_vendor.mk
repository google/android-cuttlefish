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

# If downstream target does not want telephony, do not include this file

$(call inherit-product, $(SRC_TARGET_DIR)/product/telephony_vendor.mk)

PRODUCT_VENDOR_PROPERTIES += \
    keyguard.no_require_sim=true \
    ro.cdma.home.operator.alpha=Android \
    ro.cdma.home.operator.numeric=302780 \
    ro.com.android.dataroaming=true \
    ro.telephony.default_network=9 \

ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_PACKAGES += com.google.cf.rild
else
# If downstream target provides its own RILD, set TARGET_USES_CF_RILD := false
# If the target prefers vendor APEX, this feature is not supported
TARGET_USES_CF_RILD ?= true
ifeq ($(TARGET_USES_CF_RILD),true)
PRODUCT_PACKAGES += \
    libcuttlefish-ril-2 \
    libcuttlefish-rild
endif
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.telephony.ims.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.ims.xml \
    frameworks/native/data/etc/android.hardware.telephony.satellite.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.satellite.xml
endif
