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

# TODO(b/275113769): The 'wear' targets currently enforce that APEX files are flattened.
# As riscv64 targets currently do not support this, this is a lazy-default-init that can
# be overridden in target files. Once support is enabled, require the override.
OVERRIDE_TARGET_FLATTEN_APEX ?= true

$(call inherit-product, $(SRC_TARGET_DIR)/product/base_system.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_default.mk)
$(call inherit-product-if-exists, external/hyphenation-patterns/patterns.mk)
$(call inherit-product-if-exists, external/noto-fonts/fonts.mk)
$(call inherit-product-if-exists, external/roboto-fonts/fonts.mk)
$(call inherit-product-if-exists, frameworks/base/data/keyboards/keyboards.mk)
$(call inherit-product-if-exists, frameworks/base/data/fonts/fonts.mk)
$(call inherit-product-if-exists, vendor/google/security/adb/vendor_key.mk)

PRODUCT_PACKAGES += \
    BlockedNumberProvider \
    Bluetooth \
    CalendarProvider \
    CertInstaller \
    clatd \
    clatd.conf \
    DownloadProvider \
    fsck.f2fs \
    FusedLocation \
    InputDevices \
    KeyChain \
    librs_jni \
    ManagedProvisioning \
    MmsService \
    netutils-wrapper-1.0 \
    screenrecord \
    StatementService \
    TelephonyProvider \
    TeleService \
    UserDictionaryProvider \

PRODUCT_HOST_PACKAGES += \
    fsck.f2fs \

PRODUCT_SYSTEM_SERVER_APPS += \
    FusedLocation \
    InputDevices \
    KeyChain \
    Telecom \

PRODUCT_SYSTEM_SERVER_JARS += \
    services \

PRODUCT_COPY_FILES += \
    system/core/rootdir/etc/public.libraries.wear.txt:system/etc/public.libraries.txt \
    system/core/rootdir/init.zygote32.rc:system/etc/init/hw/init.zygote32.rc \

PRODUCT_USE_DYNAMIC_PARTITION_SIZE := true

PRODUCT_ENFORCE_RRO_TARGETS := *

PRODUCT_BRAND := generic

PRODUCT_SYSTEM_NAME := mainline
PRODUCT_SYSTEM_BRAND := Android
PRODUCT_SYSTEM_MANUFACTURER := Android
PRODUCT_SYSTEM_MODEL := mainline
PRODUCT_SYSTEM_DEVICE := generic
