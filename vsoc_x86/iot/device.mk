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

$(call inherit-product, device/google/cuttlefish/shared/device.mk)
$(call inherit-product, device/google/iot/iot_base.mk)
$(call inherit-product, device/google/iot/iot_board_config_base.mk)

PRODUCT_NAME := aosp_cf_x86_iot
PRODUCT_DEVICE := vsoc_x86
PRODUCT_MODEL := Cuttlefish x86 IoT
PRODUCT_PACKAGE_OVERLAYS := device/google/cuttlefish/vsoc_x86/iot/overlay

# Boot animation.
PRODUCT_COPY_FILES += \
    device/google/iot/bootanimation.zip:/system/media/bootanimation.zip

PRODUCT_FULL_TREBLE_OVERRIDE := false
