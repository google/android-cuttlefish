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
# arm (32-bit only) target for Cuttlefish
#

TARGET_BOARD_PLATFORM := vsoc_arm_only
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a32

HOST_CROSS_OS := linux_bionic
HOST_CROSS_ARCH := arm64
HOST_CROSS_2ND_ARCH :=


TARGET_KERNEL_ARCH ?= $(TARGET_ARCH)
TARGET_KERNEL_USE ?= mainline
KERNEL_MODULES_PATH := device/google/cuttlefish_prebuilts/kernel/$(TARGET_KERNEL_USE)-$(TARGET_KERNEL_ARCH)
TARGET_KERNEL_PATH := $(KERNEL_MODULES_PATH)/kernel-$(TARGET_KERNEL_USE)

-include device/google/cuttlefish/shared/minidroid/BoardConfig.mk
