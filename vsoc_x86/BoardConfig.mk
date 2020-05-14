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
# x86 target for Cuttlefish
#

-include device/google/cuttlefish/shared/BoardConfig.mk

TARGET_BOARD_PLATFORM := vsoc_x86
TARGET_ARCH := x86
TARGET_ARCH_VARIANT := x86
TARGET_CPU_ABI := x86

TARGET_NATIVE_BRIDGE_ARCH := arm
TARGET_NATIVE_BRIDGE_ARCH_VARIANT := armv7-a-neon
TARGET_NATIVE_BRIDGE_CPU_VARIANT := generic
TARGET_NATIVE_BRIDGE_ABI := armeabi-v7a armeabi

BUILD_BROKEN_DUP_RULES := true
BOARD_VENDOR_RAMDISK_KERNEL_MODULES += $(wildcard device/google/cuttlefish_kernel/5.4-x86_64/*.ko)

# TODO(b/149410031): temporarily exclude sdcardfs
BOARD_VENDOR_RAMDISK_KERNEL_MODULES := $(filter-out %/sdcardfs.ko,$(BOARD_VENDOR_RAMDISK_KERNEL_MODULES))

# TODO(b/156534160): Temporarily allow for the old style PRODUCT_COPY_FILES for ndk_translation_prebuilt
ifeq ($(USE_NDK_TRANSLATION_BINARY),true)
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
endif
