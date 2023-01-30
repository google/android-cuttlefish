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

# FIXME: Split up and merge back in with shared/BoardConfig.mk

TARGET_KERNEL_USE ?= 5.15
TARGET_KERNEL_ARCH ?= $(TARGET_ARCH)
TARGET_KERNEL_PATH ?= kernel/prebuilts/$(TARGET_KERNEL_USE)/$(TARGET_KERNEL_ARCH)/kernel-$(TARGET_KERNEL_USE)
KERNEL_MODULES_PATH ?= \
    kernel/prebuilts/common-modules/virtual-device/$(TARGET_KERNEL_USE)/$(subst _,-,$(TARGET_KERNEL_ARCH))
PRODUCT_COPY_FILES += $(TARGET_KERNEL_PATH):kernel

# The list of modules strictly/only required either to reach second stage
# init, OR for recovery. Do not use this list to workaround second stage
# issues.
RAMDISK_KERNEL_MODULES := \
    failover.ko \
    net_failover.ko \
    virtio_blk.ko \
    virtio_console.ko \
    virtio_net.ko \
    virtio_pci.ko \
    virtio_pci_modern_dev.ko \
    virtio-rng.ko \
    vmw_vsock_virtio_transport.ko \

BOARD_VENDOR_RAMDISK_KERNEL_MODULES := \
    $(patsubst %,$(KERNEL_MODULES_PATH)/%,$(RAMDISK_KERNEL_MODULES))

TARGET_NO_RECOVERY := true

BOARD_VENDOR_RAMDISK_KERNEL_MODULES_BLOCKLIST_FILE := \
    device/google/cuttlefish/shared/modules.blocklist

TARGET_BOOTLOADER_BOARD_NAME := cutf

BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4

# Disable sparse on all filesystem images
# This will prevent sparsing of super.img
TARGET_USERIMAGES_SPARSE_EROFS_DISABLED ?= true
TARGET_USERIMAGES_SPARSE_EXT_DISABLED ?= true
TARGET_USERIMAGES_SPARSE_F2FS_DISABLED ?= true

# FIXME: Not needed for minidroid, but needs fixes to CF assembler
BOARD_USERDATAIMAGE_PARTITION_SIZE := 67108864
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_USERIMAGES_USE_EXT4 := true

BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_INIT_BOOT_IMAGE_PARTITION_SIZE := 8388608
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 67108864

BOARD_AVB_ENABLE := true
BOARD_AVB_ALGORITHM := SHA256_RSA4096
BOARD_AVB_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem

BOARD_AVB_VBMETA_SYSTEM := system
BOARD_AVB_VBMETA_SYSTEM_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_VBMETA_SYSTEM_ALGORITHM := SHA256_RSA4096
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX_LOCATION := 1

BOARD_AVB_BOOT_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_BOOT_ALGORITHM := SHA256_RSA4096
BOARD_AVB_BOOT_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_BOOT_ROLLBACK_INDEX_LOCATION := 2

BOARD_AVB_INIT_BOOT_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_INIT_BOOT_ALGORITHM := SHA256_RSA4096
BOARD_AVB_INIT_BOOT_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_INIT_BOOT_ROLLBACK_INDEX_LOCATION := 3

TARGET_AVB_SYSTEM_HASHTREE_ALGORITHM ?= sha256
BOARD_AVB_SYSTEM_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm $(TARGET_AVB_SYSTEM_HASHTREE_ALGORITHM)

BOARD_MALLOC_ALIGNMENT := 16

BOARD_USES_GENERIC_KERNEL_IMAGE := true

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/dtb.img:dtb.img \
    device/google/cuttlefish/required_images:required_images \

BOARD_BOOTLOADER_IN_UPDATE_PACKAGE := true
BOARD_RAMDISK_USE_LZ4 := true

BOARD_KERNEL_CMDLINE += printk.devkmsg=on
BOARD_KERNEL_CMDLINE += audit=1
BOARD_KERNEL_CMDLINE += panic=-1
BOARD_KERNEL_CMDLINE += 8250.nr_uarts=1
BOARD_KERNEL_CMDLINE += cma=0
BOARD_KERNEL_CMDLINE += firmware_class.path=/vendor/etc/
BOARD_KERNEL_CMDLINE += loop.max_part=7
BOARD_KERNEL_CMDLINE += init=/init
BOARD_BOOTCONFIG += androidboot.hardware=minidroid
BOARD_BOOTCONFIG += kernel.mac80211_hwsim.radios=0
BOARD_BOOTCONFIG += \
    kernel.vmw_vsock_virtio_transport_common.virtio_transport_max_vsock_pkt_buf_size=16384
BOARD_BOOTCONFIG += \
    androidboot.init_rc=/system/etc/init/hw/init.minidroid.rc
BOARD_BOOTCONFIG += \
    androidboot.microdroid.debuggable=1 \
    androidboot.adb.enabled=1

BOARD_INCLUDE_DTB_IN_BOOTIMG := true
BOARD_BOOT_HEADER_VERSION := 4
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
BOARD_INIT_BOOT_HEADER_VERSION := 4
BOARD_MKBOOTIMG_INIT_ARGS += --header_version $(BOARD_INIT_BOOT_HEADER_VERSION)

BOARD_GOOGLE_SYSTEM_DYNAMIC_PARTITIONS_PARTITION_LIST := system vendor
# reserve 256MiB for dynamic partition metadata
BOARD_GOOGLE_SYSTEM_DYNAMIC_PARTITIONS_SIZE := 268435456

# 1MiB bigger than the dynamic partition to make build happy...
BOARD_SUPER_PARTITION_SIZE := 269484032
BOARD_SUPER_PARTITION_GROUPS := google_system_dynamic_partitions
BOARD_BUILD_SUPER_IMAGE_BY_DEFAULT := true
BOARD_SUPER_IMAGE_IN_UPDATE_PACKAGE := true

TARGET_SKIP_OTA_PACKAGE := true
TARGET_SKIP_OTATOOLS_PACKAGE := true
