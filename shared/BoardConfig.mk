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

TARGET_BOOTLOADER_BOARD_NAME := cutf

# Boot partition size: 32M
# This is only used for OTA update packages. The image size on disk
# will not change (as is it not a filesystem.)
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864

# Build a separate vendor.img partition
BOARD_USES_VENDORIMAGE := true
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_COPY_OUT_VENDOR := vendor

BOARD_USES_METADATA_PARTITION := true

# Build a separate product.img partition
BOARD_USES_PRODUCTIMAGE := true
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_COPY_OUT_PRODUCT := product

# Build a separate system_ext.img partition
BOARD_USES_SYSTEM_EXTIMAGE := true
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_COPY_OUT_SYSTEM_EXT := system_ext

ifeq ($(TARGET_BUILD_SYSTEM_ROOT_IMAGE),true)
BOARD_BUILD_SYSTEM_ROOT_IMAGE := true
endif
BOARD_USES_GENERIC_AUDIO := false
USE_CAMERA_STUB := true
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_SPARSE_EXT_DISABLED := true
TARGET_USES_64_BIT_BINDER := true

# Hardware composer configuration
TARGET_USES_HWC2 := true

# The compiler will occasionally generate movaps, etc.
BOARD_MALLOC_ALIGNMENT := 16

# Make the userdata partition 4G to accomodate ASAN and CTS
BOARD_USERDATAIMAGE_PARTITION_SIZE := 4294967296

# Cache partition size: 64M
BOARD_CACHEIMAGE_PARTITION_SIZE := 67108864
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4

BOARD_GPU_DRIVERS := virgl

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

WITH_DEXPREOPT := true

USE_OPENGL_RENDERER := true

# Wifi.
BOARD_WLAN_DEVICE           := wlan0
BOARD_HOSTAPD_DRIVER        := NL80211
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
WPA_SUPPLICANT_VERSION      := VER_0_8_X
WIFI_DRIVER_FW_PATH_PARAM   := "/dev/null"
WIFI_DRIVER_FW_PATH_STA     := "/dev/null"
WIFI_DRIVER_FW_PATH_AP      := "/dev/null"

BOARD_SEPOLICY_DIRS += device/google/cuttlefish/shared/sepolicy/vendor
PRODUCT_PRIVATE_SEPOLICY_DIRS := device/google/cuttlefish/shared/sepolicy/private
# TODO(b/131193755) remove the following line.
PRODUCT_PUBLIC_SEPOLICY_DIRS := device/google/cuttlefish/shared/sepolicy/public

# master has breaking changes in dlfcn.h, but the platform SDK hasn't been
# bumped. Restore the line below when it is.
VSOC_VERSION_CFLAGS := -DVSOC_PLATFORM_SDK_VERSION=29
# VSOC_VERSION_CFLAGS := -DVSOC_PLATFORM_SDK_VERSION=${PLATFORM_SDK_VERSION}
VSOC_STLPORT_INCLUDES :=
VSOC_STLPORT_LIBS :=
VSOC_STLPORT_STATIC_LIBS :=
VSOC_TEST_INCLUDES := external/googletest/googlemock/include external/googletest/googletest/include
VSOC_TEST_LIBRARIES := libgmock_main_host libgtest_host libgmock_host
VSOC_LIBCXX_STATIC := libc++_static
VSOC_PROTOBUF_SHARED_LIB := libprotobuf-cpp-full

CUTTLEFISH_LIBRIL_NAME := libril-cuttlefish-fork
ENABLE_CUTTLEFISH_RILD := true

# TODO(ender): Remove all these once we stop depending on GCE code.
GCE_VERSION_CFLAGS := -DGCE_PLATFORM_SDK_VERSION=${PLATFORM_SDK_VERSION}
GCE_STLPORT_INCLUDES := $(VSOC_STLPORT_INCLUDES)
GCE_STLPORT_LIBS := $(VSOC_STLPORT_LIBS)
GCE_STLPORT_STATIC_LIBS := $(VSOC_STLPORT_STATIC_LIBS)
GCE_TEST_INCLUDES := $(VSOC_TEST_INCLUDES)
GCE_TEST_LIBRARIES := $(VSOC_TEST_LIBRARIES)
GCE_LIBCXX_STATIC := $(VSOC_LIBCXX_STATIC)
GCE_PROTOBUF_SHARED_LIB := $(VSOC_PROTOBUF_SHARED_LIB)
# TODO(ender): up till here.

STAGEFRIGHT_AVCENC_CFLAGS := -DANDROID_GCE

INIT_BOOTCHART := true

# Need this so that the application's loop on reading input can be synchronized
# with HW VSYNC
TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK := true

# Settings for dhcpcd-6.8.2.
DHCPCD_USE_IPV6 := no
DHCPCD_USE_DBUS := no
DHCPCD_USE_SCRIPT := yes

USE_XML_AUDIO_POLICY_CONF := 1

BOARD_VNDK_VERSION := current

# TODO(b/73078796): remove
BOARD_PROPERTY_OVERRIDES_SPLIT_ENABLED := true

TARGET_RECOVERY_PIXEL_FORMAT := ABGR_8888
ifeq ($(TARGET_BUILD_SYSTEM_ROOT_IMAGE),true)
# Use the initrd version for the dtb build, because we need to have /system
# defined somewhere, and the dtb fstab doesn't define it (deliberately)
TARGET_RECOVERY_FSTAB ?= device/google/cuttlefish/shared/config/fstab.initrd
else ifeq ($(TARGET_USE_DYNAMIC_PARTITIONS),true)
TARGET_RECOVERY_FSTAB ?= device/google/cuttlefish/shared/config/fstab.initrd-dynamic-partitions
else
TARGET_RECOVERY_FSTAB ?= device/google/cuttlefish/shared/config/fstab.initrd
endif

ifeq ($(TARGET_USE_DYNAMIC_PARTITIONS),true)
  BOARD_SUPER_PARTITION_SIZE := 6442450944
  BOARD_SUPER_PARTITION_GROUPS := google_dynamic_partitions
  BOARD_GOOGLE_DYNAMIC_PARTITIONS_PARTITION_LIST := system vendor product system_ext
  BOARD_GOOGLE_DYNAMIC_PARTITIONS_SIZE := 6442450944
  BOARD_SUPER_PARTITION_METADATA_DEVICE := vda
  BOARD_BUILD_SUPER_IMAGE_BY_DEFAULT := true
  BOARD_SUPER_IMAGE_IN_UPDATE_PACKAGE := true
  TARGET_RELEASETOOLS_EXTENSIONS := device/google/cuttlefish/shared
else
  # No dynamic partitions support; we must specify maximum sizes
  BOARD_SYSTEMIMAGE_PARTITION_SIZE := 2147483648 # 2GB
  BOARD_VENDORIMAGE_PARTITION_SIZE := 536870912 # 512MB
  BOARD_PRODUCTIMAGE_PARTITION_SIZE := 2147483648 # 2GB
  BOARD_SYSTEM_EXTIMAGE_PARTITION_SIZE := 1610612736 # 1.5GB
  TARGET_NO_RECOVERY ?= true
endif

# To see full logs from init, disable ratelimiting.
# The default is 5 messages per second amortized, with a burst of up to 10.
BOARD_KERNEL_CMDLINE += printk.devkmsg=on
