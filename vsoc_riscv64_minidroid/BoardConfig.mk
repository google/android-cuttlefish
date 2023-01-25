#
# Copyright 2022 The Android Open-Source Project
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
# riscv64 (64-bit only) target for Cuttlefish
#

TARGET_BOARD_PLATFORM := vsoc_riscv64
TARGET_ARCH := riscv64
TARGET_ARCH_VARIANT :=
TARGET_CPU_VARIANT := generic
TARGET_CPU_ABI := riscv64

# Temporary hack while prebuilt modules are missing riscv64.
ALLOW_MISSING_DEPENDENCIES := true

TARGET_KERNEL_ARCH ?= $(TARGET_ARCH)
TARGET_KERNEL_USE ?= mainline
KERNEL_MODULES_PATH := device/google/cuttlefish_prebuilts/kernel/$(TARGET_KERNEL_USE)-$(TARGET_KERNEL_ARCH)
TARGET_KERNEL_PATH := $(KERNEL_MODULES_PATH)/kernel-$(TARGET_KERNEL_USE)
# FIXME: system_dlkm should be specified as well

-include device/google/cuttlefish/shared/minidroid/BoardConfig.mk
