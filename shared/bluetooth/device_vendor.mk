#
# Copyright (C) 2023 The Android Open Source Project
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

BOARD_HAVE_BLUETOOTH ?= true

ifeq ($(BOARD_HAVE_BLUETOOTH), true)

# If a downstream target does not want any bluetooth support, do not
# include this file!

LOCAL_BT_PROPERTIES ?= \
    vendor.ser.bt-uart?=/dev/hvc5 \

PRODUCT_VENDOR_PROPERTIES += \
    ${LOCAL_BT_PROPERTIES} \

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/bluetooth_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration_7_0.xml \

PRODUCT_PACKAGES += com.google.cf.bt

#
# Bluetooth Audio AIDL HAL
#
PRODUCT_PACKAGES += \
    android.hardware.bluetooth.audio-impl \

else # BOARD_HAVE_BLUETOOTH == true

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/bluetooth/excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/bluetooth_excluded_hardware.xml \

endif # BOARD_HAVE_BLUETOOTH == true
