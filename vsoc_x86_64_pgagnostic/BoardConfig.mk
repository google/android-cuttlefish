#
# Copyright 2023 The Android Open-Source Project
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
# x86_64 (64-bit only) page size agnostic target for Cuttlefish
#

TARGET_BOARD_PLATFORM := vsoc_x86_64
TARGET_ARCH := x86_64
TARGET_ARCH_VARIANT := silvermont
TARGET_CPU_ABI := x86_64

TARGET_NATIVE_BRIDGE_ARCH := arm64
TARGET_NATIVE_BRIDGE_ARCH_VARIANT := armv8-a
TARGET_NATIVE_BRIDGE_CPU_VARIANT := generic
TARGET_NATIVE_BRIDGE_ABI := arm64-v8a

# Use 6.6 kernel
TARGET_KERNEL_USE ?= 6.6
TARGET_KERNEL_ARCH ?= x86_64
SYSTEM_DLKM_SRC ?= kernel/prebuilts/$(TARGET_KERNEL_USE)/$(TARGET_KERNEL_ARCH)
TARGET_KERNEL_PATH ?= $(SYSTEM_DLKM_SRC)/kernel-$(TARGET_KERNEL_USE)
KERNEL_MODULES_PATH ?= \
    kernel/prebuilts/common-modules/virtual-device/$(TARGET_KERNEL_USE)/$(subst _,-,$(TARGET_KERNEL_ARCH))

# Emulate 16KB page size
BOARD_KERNEL_CMDLINE += androidboot.page_shift=14

TARGET_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_RO_FILE_SYSTEM_TYPE := ext4

AUDIOSERVER_MULTILIB := first

-include device/google/cuttlefish/shared/BoardConfig.mk
-include device/google/cuttlefish/shared/bluetooth/BoardConfig.mk
-include device/google/cuttlefish/shared/camera/BoardConfig.mk
-include device/google/cuttlefish/shared/gnss/BoardConfig.mk
-include device/google/cuttlefish/shared/graphics/BoardConfig.mk
-include device/google/cuttlefish/shared/identity/BoardConfig.mk
-include device/google/cuttlefish/shared/reboot_escrow/BoardConfig.mk
-include device/google/cuttlefish/shared/sensors/BoardConfig.mk
-include device/google/cuttlefish/shared/telephony/BoardConfig.mk
-include device/google/cuttlefish/shared/vibrator/BoardConfig.mk

ifneq ($(BOARD_IS_AUTOMOTIVE), true)
-include device/google/cuttlefish/shared/swiftshader/BoardConfig.mk
-include device/google/cuttlefish/shared/virgl/BoardConfig.mk
endif
