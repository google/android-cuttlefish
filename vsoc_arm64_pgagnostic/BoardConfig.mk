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
# arm64 (64-bit only) page size agnostic target for Cuttlefish
#

TARGET_BOARD_PLATFORM := vsoc_arm64
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := cortex-a53

# Use 16K page size kernel
TARGET_KERNEL_USE ?= 6.6
TARGET_KERNEL_ARCH ?= arm64
SYSTEM_DLKM_SRC ?= kernel/prebuilts/$(TARGET_KERNEL_USE)/$(TARGET_KERNEL_ARCH)/16k
TARGET_KERNEL_PATH ?= $(SYSTEM_DLKM_SRC)/kernel-$(TARGET_KERNEL_USE)
KERNEL_MODULES_PATH ?= \
    kernel/prebuilts/common-modules/virtual-device/$(TARGET_KERNEL_USE)/$(subst _,-,$(TARGET_KERNEL_ARCH))/16k

TARGET_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_RO_FILE_SYSTEM_TYPE := ext4

BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4

AUDIOSERVER_MULTILIB := first

HOST_CROSS_OS := linux_musl
HOST_CROSS_ARCH := arm64
HOST_CROSS_2ND_ARCH :=

-include device/google/cuttlefish/shared/BoardConfig.mk
-include device/google/cuttlefish/shared/bluetooth/BoardConfig.mk
-include device/google/cuttlefish/shared/camera/BoardConfig.mk
-include device/google/cuttlefish/shared/graphics/BoardConfig.mk
-include device/google/cuttlefish/shared/identity/BoardConfig.mk
-include device/google/cuttlefish/shared/sensors/BoardConfig.mk
-include device/google/cuttlefish/shared/swiftshader/BoardConfig.mk
-include device/google/cuttlefish/shared/telephony/BoardConfig.mk
-include device/google/cuttlefish/shared/virgl/BoardConfig.mk

BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_SYSTEMIMAGE_EROFS_BLOCKSIZE := 16384
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := erofs

BOARD_F2FS_BLOCKSIZE := 16384
$(call soong_config_append,cvdhost,board_f2fs_blocksize,16384)
