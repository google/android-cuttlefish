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

ifneq ($(TARGET_NO_TELEPHONY), true)

$(call inherit-product, $(SRC_TARGET_DIR)/product/telephony_vendor.mk)

# Include the package for Cuttlefish telephony satellite configurations.
PRODUCT_PACKAGES += \
    cuttlefish_telephony_satellite_configs

# Include the Soong namespace for Cuttlefish satellite telephony configurations.
PRODUCT_SOONG_NAMESPACES += \
    device/google/cuttlefish/shared/telephony/satellite

# If downstream target provides its own RILD, set TARGET_USES_CF_RILD := false
TARGET_USES_CF_RILD ?= true
ifeq ($(TARGET_USES_CF_RILD),true)
    # NR 5G, LTE, TD-SCDMA, CDMA, EVDO, GSM and WCDMA
    PRODUCT_VENDOR_PROPERTIES += ro.telephony.default_network=33

    PRODUCT_SOONG_NAMESPACES += \
        device/google/cuttlefish/apex/com.google.cf.rild

    PRODUCT_PACKAGES += com.google.cf.rild
endif

endif # if not TARGET_NO_TELEPHONY
