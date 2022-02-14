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

$(call inherit-product, $(SRC_TARGET_DIR)/product/base_product.mk)

# Default AOSP sounds
$(call inherit-product-if-exists, frameworks/base/data/sounds/AllAudio.mk)

# Wear pulls in some obsolete samples as well
_ringtones := Callisto Dione Ganymede Luna Oberon Phobos Sedna Triton Umbriel
PRODUCT_COPY_FILES += \
    frameworks/base/data/sounds/alarms/ogg/Oxygen.ogg:$(TARGET_COPY_OUT_PRODUCT)/media/audio/alarms/Oxygen.ogg \
    frameworks/base/data/sounds/notifications/ogg/Tethys.ogg:$(TARGET_COPY_OUT_PRODUCT)/media/audio/notifications/Tethys.ogg \
    $(call product-copy-files-by-pattern,frameworks/base/data/sounds/ringtones/ogg/%.ogg,$(TARGET_COPY_OUT_PRODUCT)/media/audio/ringtones/%.ogg,$(_ringtones)) \

PRODUCT_PRODUCT_PROPERTIES += \
    ro.config.alarm_alert=Oxygen.ogg \
    ro.config.notification_sound=Tethys.ogg \
    ro.config.ringtone=Atria.ogg \

PRODUCT_COPY_FILES += \
    device/sample/etc/apns-full-conf.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/apns-conf.xml \
