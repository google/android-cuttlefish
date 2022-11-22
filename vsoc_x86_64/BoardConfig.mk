#
# Copyright 2017 The Android Open-Source Project
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
# x86_64  target for Cuttlefish
#

-include device/google/cuttlefish/shared/BoardConfig.mk
-include device/google/cuttlefish/shared/camera/BoardConfig.mk
-include device/google/cuttlefish/shared/swiftshader/BoardConfig.mk
-include device/google/cuttlefish/shared/telephony/BoardConfig.mk

TARGET_BOARD_PLATFORM := vsoc_x86_64
TARGET_ARCH := x86_64
TARGET_ARCH_VARIANT := silvermont
TARGET_CPU_ABI := x86_64

TARGET_2ND_ARCH := x86
TARGET_2ND_CPU_ABI := x86
TARGET_2ND_ARCH_VARIANT := silvermont

TARGET_NATIVE_BRIDGE_ARCH := arm64
TARGET_NATIVE_BRIDGE_ARCH_VARIANT := armv8-a
TARGET_NATIVE_BRIDGE_CPU_VARIANT := generic
TARGET_NATIVE_BRIDGE_ABI := arm64-v8a

TARGET_NATIVE_BRIDGE_2ND_ARCH := arm
TARGET_NATIVE_BRIDGE_2ND_ARCH_VARIANT := armv7-a-neon
TARGET_NATIVE_BRIDGE_2ND_CPU_VARIANT := generic
TARGET_NATIVE_BRIDGE_2ND_ABI := armeabi-v7a armeabi

KERNEL_MODULES_PATH := kernel/prebuilts/common-modules/virtual-device/$(TARGET_KERNEL_USE)/x86-64


ifeq ($(BOARD_VENDOR_RAMDISK_KERNEL_MODULES),)
  BOARD_VENDOR_RAMDISK_KERNEL_MODULES := $(patsubst %, $(KERNEL_MODULES_PATH)/%, $(RAMDISK_KERNEL_MODULES))
  ALL_KERNEL_MODULES := $(wildcard $(KERNEL_MODULES_PATH)/*.ko)
  BOARD_VENDOR_KERNEL_MODULES := $(filter-out $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES),$(ALL_KERNEL_MODULES))
endif
