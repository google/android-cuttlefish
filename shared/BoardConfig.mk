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
# Common BoardConfig for all supported architectures.
#

TARGET_KERNEL_USE ?= 6.1
TARGET_KERNEL_ARCH ?= $(TARGET_ARCH)
SYSTEM_DLKM_SRC ?= kernel/prebuilts/$(TARGET_KERNEL_USE)/$(TARGET_KERNEL_ARCH)
TARGET_KERNEL_PATH ?= $(SYSTEM_DLKM_SRC)/kernel-$(TARGET_KERNEL_USE)
KERNEL_MODULES_PATH ?= \
    kernel/prebuilts/common-modules/virtual-device/$(TARGET_KERNEL_USE)/$(subst _,-,$(TARGET_KERNEL_ARCH))
PRODUCT_COPY_FILES += $(TARGET_KERNEL_PATH):kernel

# The list of modules strictly/only required either to reach second stage
# init, OR for recovery. Do not use this list to workaround second stage
# issues.
RAMDISK_KERNEL_MODULES := \
    failover.ko \
    nd_virtio.ko \
    net_failover.ko \
    virtio_blk.ko \
    virtio_console.ko \
    virtio_dma_buf.ko \
    virtio-gpu.ko \
    virtio_input.ko \
    virtio_net.ko \
    virtio_pci.ko \
    virtio-rng.ko \
    vmw_vsock_virtio_transport.ko \

BOARD_VENDOR_RAMDISK_KERNEL_MODULES := \
    $(patsubst %,$(KERNEL_MODULES_PATH)/%,$(RAMDISK_KERNEL_MODULES))

# GKI >5.15 will have and require virtio_pci_legacy_dev.ko
BOARD_VENDOR_RAMDISK_KERNEL_MODULES += $(wildcard $(KERNEL_MODULES_PATH)/virtio_pci_legacy_dev.ko)
# GKI >5.10 will have and require virtio_pci_modern_dev.ko
BOARD_VENDOR_RAMDISK_KERNEL_MODULES += $(wildcard $(KERNEL_MODULES_PATH)/virtio_pci_modern_dev.ko)

ALL_KERNEL_MODULES := $(wildcard $(KERNEL_MODULES_PATH)/*.ko)
BOARD_VENDOR_KERNEL_MODULES := \
    $(filter-out $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES),\
                 $(wildcard $(KERNEL_MODULES_PATH)/*.ko))

# TODO(b/170639028): Back up TARGET_NO_BOOTLOADER
__TARGET_NO_BOOTLOADER := $(TARGET_NO_BOOTLOADER)
include build/make/target/board/BoardConfigMainlineCommon.mk
TARGET_NO_BOOTLOADER := $(__TARGET_NO_BOOTLOADER)

BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE := \
    device/google/cuttlefish/shared/modules.blocklist

ifndef TARGET_BOOTLOADER_BOARD_NAME
TARGET_BOOTLOADER_BOARD_NAME := cutf
endif

BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := $(TARGET_RO_FILE_SYSTEM_TYPE)

# Boot partition size: 64M
# This is only used for OTA update packages. The image size on disk
# will not change (as is it not a filesystem.)
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
ifdef TARGET_DEDICATED_RECOVERY
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864
endif
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 67108864

# init_boot partition size is recommended to be 8MB, it can be larger.
# When this variable is set, init_boot.img will be built with the generic
# ramdisk, and that ramdisk will no longer be included in boot.img.
BOARD_INIT_BOOT_IMAGE_PARTITION_SIZE := 8388608

# Build a separate vendor.img partition
BOARD_USES_VENDORIMAGE := true
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := $(TARGET_RO_FILE_SYSTEM_TYPE)

BOARD_USES_METADATA_PARTITION := true

# Build a separate product.img partition
BOARD_USES_PRODUCTIMAGE := true
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := $(TARGET_RO_FILE_SYSTEM_TYPE)

# Build a separate system_ext.img partition
BOARD_USES_SYSTEM_EXTIMAGE := true
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := $(TARGET_RO_FILE_SYSTEM_TYPE)
TARGET_COPY_OUT_SYSTEM_EXT := system_ext

# Build a separate odm.img partition
BOARD_USES_ODMIMAGE := true
BOARD_ODMIMAGE_FILE_SYSTEM_TYPE := $(TARGET_RO_FILE_SYSTEM_TYPE)
TARGET_COPY_OUT_ODM := odm

# Build a separate vendor_dlkm partition
BOARD_USES_VENDOR_DLKMIMAGE := true
BOARD_VENDOR_DLKMIMAGE_FILE_SYSTEM_TYPE := $(TARGET_RO_FILE_SYSTEM_TYPE)
TARGET_COPY_OUT_VENDOR_DLKM := vendor_dlkm

# Build a separate odm_dlkm partition
BOARD_USES_ODM_DLKMIMAGE := true
BOARD_ODM_DLKMIMAGE_FILE_SYSTEM_TYPE := $(TARGET_RO_FILE_SYSTEM_TYPE)
TARGET_COPY_OUT_ODM_DLKM := odm_dlkm

# Build a separate system_dlkm partition
BOARD_USES_SYSTEM_DLKMIMAGE := true
BOARD_SYSTEM_DLKMIMAGE_FILE_SYSTEM_TYPE := $(TARGET_RO_FILE_SYSTEM_TYPE)
TARGET_COPY_OUT_SYSTEM_DLKM := system_dlkm
BOARD_SYSTEM_KERNEL_MODULES := $(wildcard $(SYSTEM_DLKM_SRC)/*.ko)

# Enable AVB
BOARD_AVB_ENABLE := true
BOARD_AVB_ALGORITHM := SHA256_RSA4096
BOARD_AVB_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem

# Enable chained vbmeta for system image mixing
BOARD_AVB_VBMETA_SYSTEM := product system system_ext
BOARD_AVB_VBMETA_SYSTEM_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_VBMETA_SYSTEM_ALGORITHM := SHA256_RSA4096
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX_LOCATION := 1

# Enable chained vbmeta for boot images
BOARD_AVB_BOOT_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_BOOT_ALGORITHM := SHA256_RSA4096
BOARD_AVB_BOOT_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_BOOT_ROLLBACK_INDEX_LOCATION := 2

# Enable chained vbmeta for init_boot images
BOARD_AVB_INIT_BOOT_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_INIT_BOOT_ALGORITHM := SHA256_RSA4096
BOARD_AVB_INIT_BOOT_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_INIT_BOOT_ROLLBACK_INDEX_LOCATION := 3

# Enabled chained vbmeta for vendor_dlkm
BOARD_AVB_VBMETA_CUSTOM_PARTITIONS := vendor_dlkm system_dlkm
BOARD_AVB_VBMETA_VENDOR_DLKM := vendor_dlkm
BOARD_AVB_VBMETA_VENDOR_DLKM_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_VBMETA_VENDOR_DLKM_ALGORITHM := SHA256_RSA4096
BOARD_AVB_VBMETA_VENDOR_DLKM_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_VBMETA_VENDOR_DLKM_ROLLBACK_INDEX_LOCATION := 4

BOARD_AVB_VBMETA_SYSTEM_DLKM := system_dlkm
BOARD_AVB_VBMETA_SYSTEM_DLKM_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_VBMETA_SYSTEM_DLKM_ALGORITHM := SHA256_RSA4096
BOARD_AVB_VBMETA_SYSTEM_DLKM_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_VBMETA_SYSTEM_DLKM_ROLLBACK_INDEX_LOCATION := 5


# Using sha256 for dm-verity partitions. b/178983355
# system, system_other, product.
TARGET_AVB_SYSTEM_HASHTREE_ALGORITHM ?= sha256
TARGET_AVB_SYSTEM_OTHER_HASHTREE_ALGORITHM ?= sha256
TARGET_AVB_PRODUCT_HASHTREE_ALGORITHM ?= sha256
# Using blake2b-256 for system_ext. This give us move coverage of the
# algorithms as we otherwise don't have a device using blake2b-256.
TARGET_AVB_SYSTEM_EXT_HASHTREE_ALGORITHM ?= blake2b-256

BOARD_AVB_SYSTEM_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm $(TARGET_AVB_SYSTEM_HASHTREE_ALGORITHM)
BOARD_AVB_SYSTEM_OTHER_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm $(TARGET_AVB_SYSTEM_OTHER_HASHTREE_ALGORITHM)
BOARD_AVB_SYSTEM_EXT_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm $(TARGET_AVB_SYSTEM_EXT_HASHTREE_ALGORITHM)
BOARD_AVB_PRODUCT_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm $(TARGET_AVB_PRODUCT_HASHTREE_ALGORITHM)

# vendor and odm.
BOARD_AVB_VENDOR_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm sha256
BOARD_AVB_ODM_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm sha256

# vendor_dlkm, odm_dlkm, and system_dlkm.
BOARD_AVB_VENDOR_DLKM_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm sha256
BOARD_AVB_ODM_DLKM_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm sha256
BOARD_AVB_SYSTEM_DLKM_ADD_HASHTREE_FOOTER_ARGS += --hash_algorithm sha256

BOARD_USES_GENERIC_AUDIO := false
USE_CAMERA_STUB := true

# Hardware composer configuration
TARGET_USES_HWC2 := true

# The compiler will occasionally generate movaps, etc.
BOARD_MALLOC_ALIGNMENT := 16

# Enable sparse on all filesystem images
TARGET_USERIMAGES_SPARSE_EROFS_DISABLED ?= false
TARGET_USERIMAGES_SPARSE_EXT_DISABLED ?= false
TARGET_USERIMAGES_SPARSE_F2FS_DISABLED ?= false

# Make the userdata partition 8G to accommodate ASAN, CTS and provide
# enough space for other cases (such as remount, etc)
BOARD_USERDATAIMAGE_PARTITION_SIZE := $(TARGET_USERDATAIMAGE_PARTITION_SIZE)
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := $(TARGET_USERDATAIMAGE_FILE_SYSTEM_TYPE)
TARGET_USERIMAGES_USE_F2FS := true

# Enable goldfish's encoder.
# TODO(b/113617962) Remove this if we decide to use
# device/generic/opengl-transport to generate the encoder
BUILD_EMULATOR_OPENGL_DRIVER := true
BUILD_EMULATOR_OPENGL := true

# Minimum size of the final bootable disk image: 10G
# GCE will pad disk images out to 10G. Our disk images should be at least as
# big to avoid warnings about partition table oddities.
BOARD_DISK_IMAGE_MINIMUM_SIZE := 10737418240

BOARD_BOOTIMAGE_MAX_SIZE := 8388608
BOARD_SYSLOADER_MAX_SIZE := 7340032
# TODO(san): See if we can get rid of this.
BOARD_FLASH_BLOCK_SIZE := 512

USE_OPENGL_RENDERER := true

# Wifi.
BOARD_WLAN_DEVICE           := emulator
BOARD_HOSTAPD_PRIVATE_LIB   := lib_driver_cmd_simulated_cf
WIFI_HIDL_FEATURE_DUAL_INTERFACE := true
WIFI_HAL_INTERFACE_COMBINATIONS := {{{STA}, 1}, {{AP}, 1}, {{P2P}, 1}}
BOARD_HOSTAPD_DRIVER        := NL80211
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_simulated_cf
WPA_SUPPLICANT_VERSION      := VER_0_8_X
WIFI_DRIVER_FW_PATH_PARAM   := "/dev/null"
WIFI_DRIVER_FW_PATH_STA     := "/dev/null"
WIFI_DRIVER_FW_PATH_AP      := "/dev/null"

# vendor sepolicy
BOARD_VENDOR_SEPOLICY_DIRS += device/google/cuttlefish/shared/sepolicy/vendor
BOARD_VENDOR_SEPOLICY_DIRS += device/google/cuttlefish/shared/sepolicy/vendor/google

BOARD_SEPOLICY_DIRS += system/bt/vendor_libs/linux/sepolicy

# product sepolicy, allow other layers to append
PRODUCT_PRIVATE_SEPOLICY_DIRS += device/google/cuttlefish/shared/sepolicy/product/private
# PRODUCT_PUBLIC_SEPOLICY_DIRS += device/google/cuttlefish/shared/sepolicy/product/public
# system_ext sepolicy
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += device/google/cuttlefish/shared/sepolicy/system_ext/private
# SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += device/google/cuttlefish/shared/sepolicy/system_ext/public

STAGEFRIGHT_AVCENC_CFLAGS := -DANDROID_GCE

INIT_BOOTCHART := true

# Settings for dhcpcd-6.8.2.
DHCPCD_USE_IPV6 := no
DHCPCD_USE_DBUS := no
DHCPCD_USE_SCRIPT := yes


TARGET_RECOVERY_PIXEL_FORMAT := ABGR_8888
TARGET_RECOVERY_UI_LIB := librecovery_ui_cuttlefish
TARGET_RECOVERY_FSTAB_GENRULE := gen_fstab_cf_f2fs_cts

BOARD_SUPER_PARTITION_SIZE := 7516192768  # 7GiB
BOARD_SUPER_PARTITION_GROUPS := google_system_dynamic_partitions google_vendor_dynamic_partitions
BOARD_GOOGLE_SYSTEM_DYNAMIC_PARTITIONS_PARTITION_LIST := product system system_ext system_dlkm
BOARD_GOOGLE_SYSTEM_DYNAMIC_PARTITIONS_SIZE := 5771362304  # 5.375GiB
BOARD_GOOGLE_VENDOR_DYNAMIC_PARTITIONS_PARTITION_LIST := odm vendor vendor_dlkm odm_dlkm
# 1404MiB, reserve 4MiB for dynamic partition metadata
BOARD_GOOGLE_VENDOR_DYNAMIC_PARTITIONS_SIZE := 1472200704
BOARD_BUILD_SUPER_IMAGE_BY_DEFAULT := true
BOARD_SUPER_IMAGE_IN_UPDATE_PACKAGE := true
TARGET_RELEASETOOLS_EXTENSIONS := device/google/cuttlefish/shared

# Generate a partial ota update package for partitions in vbmeta_system
BOARD_PARTIAL_OTA_UPDATE_PARTITIONS_LIST := product system system_ext vbmeta_system

BOARD_BOOTLOADER_IN_UPDATE_PACKAGE := true
BOARD_RAMDISK_USE_LZ4 := true

# To see full logs from init, disable ratelimiting.
# The default is 5 messages per second amortized, with a burst of up to 10.
BOARD_KERNEL_CMDLINE += printk.devkmsg=on

# Print audit messages for all security check failures
BOARD_KERNEL_CMDLINE += audit=1

# Reboot immediately on panic
BOARD_KERNEL_CMDLINE += panic=-1

# Always enable one legacy serial port, for alternative earlycon, kgdb, and
# serial console. Doesn't do anything on ARM/ARM64 + QEMU or Gem5.
BOARD_KERNEL_CMDLINE += 8250.nr_uarts=1

# Cuttlefish doesn't use CMA, so don't reserve RAM for it
BOARD_KERNEL_CMDLINE += cma=0

# Default firmware load path
BOARD_KERNEL_CMDLINE += firmware_class.path=/vendor/etc/

# Needed to boot Android
BOARD_KERNEL_CMDLINE += loop.max_part=7
BOARD_KERNEL_CMDLINE += init=/init

# Enable KUnit for userdebug and eng builds
ifneq (,$(filter userdebug eng, $(TARGET_BUILD_VARIANT)))
  BOARD_KERNEL_CMDLINE += kunit.enable=1
endif

BOARD_BOOTCONFIG += androidboot.hardware=cutf_cvm

# TODO(b/182417593): Move all of these module options to modules.options
# TODO(b/176860479): Remove once goldfish and cuttlefish share a wifi implementation
BOARD_BOOTCONFIG += kernel.mac80211_hwsim.radios=0
# Reduce slab size usage from virtio vsock to reduce slab fragmentation
BOARD_BOOTCONFIG += \
    kernel.vmw_vsock_virtio_transport_common.virtio_transport_max_vsock_pkt_buf_size=16384

BOARD_BOOTCONFIG += \
    androidboot.vendor.apex.com.google.emulated.camera.provider.hal=com.google.emulated.camera.provider.hal \

BOARD_INCLUDE_DTB_IN_BOOTIMG := true
ifndef BOARD_BOOT_HEADER_VERSION
BOARD_BOOT_HEADER_VERSION := 4
endif
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
BOARD_INIT_BOOT_HEADER_VERSION := 4
BOARD_MKBOOTIMG_INIT_ARGS += --header_version $(BOARD_INIT_BOOT_HEADER_VERSION)
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/dtb.img:dtb.img \
    device/google/cuttlefish/required_images:required_images \

# Cuttlefish doesn't support ramdump feature yet, exclude the ramdump debug tool.
EXCLUDE_BUILD_RAMDUMP_UPLOADER_DEBUG_TOOL := true

# GKI-related variables.
BOARD_USES_GENERIC_KERNEL_IMAGE := true
ifdef TARGET_DEDICATED_RECOVERY
  BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE := true
else ifneq ($(PRODUCT_BUILD_VENDOR_BOOT_IMAGE), false)
  BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT := true
endif
BOARD_MOVE_GSI_AVB_KEYS_TO_VENDOR_BOOT := true

BOARD_GENERIC_RAMDISK_KERNEL_MODULES_LOAD := dm-user.ko

# Enable the new fingerprint format on cuttlefish
BOARD_USE_VBMETA_DIGTEST_IN_FINGERPRINT := true

# Set AB OTA partitions based on the build configuration
AB_OTA_UPDATER := true

ifneq ($(PRODUCT_BUILD_VENDOR_IMAGE), false)
AB_OTA_PARTITIONS += vendor
AB_OTA_PARTITIONS += vendor_dlkm
ifneq ($(BOARD_AVB_VBMETA_VENDOR_DLKM),)
AB_OTA_PARTITIONS += vbmeta_vendor_dlkm
endif
endif
ifneq ($(BOARD_AVB_VBMETA_SYSTEM_DLKM),)
AB_OTA_PARTITIONS += vbmeta_system_dlkm
endif

ifneq ($(PRODUCT_BUILD_BOOT_IMAGE), false)
AB_OTA_PARTITIONS += boot
endif

ifneq ($(PRODUCT_BUILD_INIT_BOOT_IMAGE), false)
AB_OTA_PARTITIONS += init_boot
endif

ifneq ($(PRODUCT_BUILD_VENDOR_BOOT_IMAGE), false)
AB_OTA_PARTITIONS += vendor_boot
endif

ifneq ($(PRODUCT_BUILD_ODM_IMAGE), false)
AB_OTA_PARTITIONS += odm
AB_OTA_PARTITIONS += odm_dlkm
endif

ifneq ($(PRODUCT_BUILD_PRODUCT_IMAGE), false)
AB_OTA_PARTITIONS += product
endif

ifneq ($(PRODUCT_BUILD_SYSTEM_IMAGE), false)
AB_OTA_PARTITIONS += system
AB_OTA_PARTITIONS += system_dlkm
ifneq ($(PRODUCT_BUILD_VBMETA_IMAGE), false)
AB_OTA_PARTITIONS += vbmeta_system
endif
endif

ifneq ($(PRODUCT_BUILD_SYSTEM_EXT_IMAGE), false)
AB_OTA_PARTITIONS += system_ext
endif

ifneq ($(PRODUCT_BUILD_VBMETA_IMAGE), false)
AB_OTA_PARTITIONS += vbmeta
endif
