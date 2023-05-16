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

PRODUCT_VENDOR_PROPERTIES += \
    ro.config.low_ram=true \

PRODUCT_SYSTEM_SERVER_COMPILER_FILTER := speed-profile

PRODUCT_ALWAYS_PREOPT_EXTRACTED_APK := true

PRODUCT_USE_PROFILE_FOR_BOOT_IMAGE := true
PRODUCT_DEX_PREOPT_BOOT_IMAGE_PROFILE_LOCATION := frameworks/base/config/boot-image-profile.txt

PRODUCT_ART_TARGET_INCLUDE_DEBUG_BUILD := false

PRODUCT_PACKAGES += \
    CellBroadcastAppPlatform \
    CellBroadcastServiceModulePlatform \
    com.android.tethering \
    InProcessNetworkStack \

PRODUCT_MINIMIZE_JAVA_DEBUG_INFO := true

ifneq (,$(filter eng, $(TARGET_BUILD_VARIANT)))
    PRODUCT_DISABLE_SCUDO := true
endif

TARGET_SYSTEM_PROP += device/google/cuttlefish/shared/wear/wearable-1024.prop

TARGET_VNDK_USE_CORE_VARIANT := true
