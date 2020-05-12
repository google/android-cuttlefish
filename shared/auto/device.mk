#
# Copyright (C) 2017 The Android Open Source Project
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

################################################
# Begin GCE specific configurations

DEVICE_MANIFEST_FILE += device/google/cuttlefish/shared/config/manifest.xml
DEVICE_MANIFEST_FILE += device/google/cuttlefish/shared/auto/manifest.xml

$(call inherit-product, device/google/cuttlefish/shared/device.mk)

################################################
# Begin general Android Auto Embedded configurations

PRODUCT_COPY_FILES += \
    packages/services/Car/car_product/init/init.bootstat.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw//init.bootstat.rc \
    packages/services/Car/car_product/init/init.car.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw//init.car.rc

# Auto core hardware permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/car_core_hardware.xml:system/etc/permissions/car_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.type.automotive.xml:system/etc/permissions/android.hardware.type.automotive.xml \

# Enable landscape
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.screen.landscape.xml:system/etc/permissions/android.hardware.screen.landscape.xml

# Used to embed a map in an activity view
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.software.activities_on_secondary_displays.xml:system/etc/permissions/android.software.activities_on_secondary_displays.xml

# Location permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml

# Broadcast Radio permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.broadcastradio.xml:system/etc/permissions/android.hardware.broadcastradio.xml

PRODUCT_PROPERTY_OVERRIDES += \
    keyguard.no_require_sim=true \
    ro.cdma.home.operator.alpha=Android \
    ro.cdma.home.operator.numeric=302780 \
    vendor.rild.libpath=libcuttlefish-ril.so \

# vehicle HAL
ifeq ($(LOCAL_VHAL_PRODUCT_PACKAGE),)
    LOCAL_VHAL_PRODUCT_PACKAGE := android.hardware.automotive.vehicle@2.0-service
    BOARD_SEPOLICY_DIRS += device/google/cuttlefish/shared/auto/sepolicy
endif
PRODUCT_PACKAGES += $(LOCAL_VHAL_PRODUCT_PACKAGE)

# Broadcast Radio
PRODUCT_PACKAGES += android.hardware.broadcastradio@2.0-service

# AudioControl HAL
ifeq ($(LOCAL_AUDIOCONTROL_HAL_PRODUCT_PACKAGE),)
    LOCAL_AUDIOCONTROL_HAL_PRODUCT_PACKAGE := android.hardware.automotive.audiocontrol@2.0-service
endif
PRODUCT_PACKAGES += $(LOCAL_AUDIOCONTROL_HAL_PRODUCT_PACKAGE)

# CAN bus HAL
PRODUCT_PACKAGES += android.hardware.automotive.can@1.0-service
PRODUCT_PACKAGES_DEBUG += canhalctrl \
    canhaldump \
    canhalsend

PRODUCT_PACKAGES += \
    libcuttlefish-ril \
    libcuttlefish-rild

BOARD_IS_AUTOMOTIVE := true

$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base.mk)
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
$(call inherit-product, packages/services/Car/car_product/build/car.mk)

# Placed here due to b/110784510
PRODUCT_BRAND := generic

PRODUCT_ENFORCE_RRO_TARGETS := framework-res

TARGET_NO_TELEPHONY := true
