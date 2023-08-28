#
# Copyright (C) 2023 The Android Open Source Project
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

# TODO(b/264958209): for now it's just inheriting aosp_cf_md and setting
# config_multiuserVisibleBackgroundUsersOnDefaultDisplay , but in the
# long-run it should be customized further (for example, setting
# occupancy zone and removing cluster and other unnecessary stuff)

$(call inherit-product, device/google/cuttlefish/vsoc_x86_64_only/auto_md/aosp_cf.mk)

# HSUM is currently incompatible with telephony.
# TODO(b/283853205): Properly disable telephony using per-partition makefile.
TARGET_NO_TELEPHONY := true

PRODUCT_NAME := aosp_cf_x86_64_auto_mdnd
PRODUCT_MODEL := Cuttlefish x86_64 auto 64-bit only multi-displays, no-driver

PRODUCT_PACKAGE_OVERLAYS += \
    device/google/cuttlefish/shared/auto_mdnd/overlay
