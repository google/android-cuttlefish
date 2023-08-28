#
# Copyright (C) 2022 The Android Open Source Project
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

# Set board, as displays are set in the config_BOARD.json file (in
# that file, display0 is main, display1 is cluster, and any other displays
# are passenger displays - notice that the maximum allowed is 4 total).
TARGET_BOARD_INFO_FILE := device/google/cuttlefish/shared/auto_md/android-info.txt

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/auto_md/display_settings.xml:$(TARGET_COPY_OUT_VENDOR)/etc/display_settings.xml

PRODUCT_PACKAGE_OVERLAYS += \
    device/google/cuttlefish/shared/auto_md/overlay

# HSUM is currently incompatible with telephony.
# TODO(b/283853205): Properly disable telephony using per-partition makefile.
TARGET_NO_TELEPHONY := true

ENABLE_CLUSTER_OS_DOUBLE:=true

PRODUCT_PACKAGES += \
    ClusterHomeSample \
    ClusterOsDouble \
    CarServiceOverlayEmulatorOsDouble \
    CarServiceOverlayMdEmulatorOsDouble \
    MultiDisplaySecondaryHomeTestLauncher \
    MultiDisplayTest

PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    com.android.car.internal.debug.num_auto_populated_users=1 # 1 passenger only (so 2nd display shows user picker)
# TODO(b/233370174): add audio multi-zone
#   ro.vendor.simulateMultiZoneAudio=true \


# This will disable dynamic displays and enable hardcoded displays on hwservicemanager.
$(call inherit-product, device/generic/car/emulator/cluster/cluster-hwservicemanager.mk)

# Add the regular stuff.
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64_only/auto/aosp_cf.mk)

PRODUCT_NAME := aosp_cf_x86_64_auto_md
PRODUCT_MODEL := Cuttlefish x86_64 auto 64-bit only multi-displays
