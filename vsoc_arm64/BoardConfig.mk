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
# arm64 target for Cuttlefish
#

TARGET_BOARD_PLATFORM := vsoc_arm64
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := cortex-a53
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := cortex-a53
TARGET_TRANSLATE_2ND_ARCH := false

HOST_CROSS_OS := linux_bionic
HOST_CROSS_ARCH := arm64
HOST_CROSS_2ND_ARCH :=

-include device/google/cuttlefish/shared/BoardConfig.mk
-include device/google/cuttlefish/shared/bluetooth/BoardConfig.mk
-include device/google/cuttlefish/shared/camera/BoardConfig.mk
-include device/google/cuttlefish/shared/graphics/BoardConfig.mk
-include device/google/cuttlefish/shared/swiftshader/BoardConfig.mk
-include device/google/cuttlefish/shared/telephony/BoardConfig.mk
-include device/google/cuttlefish/shared/virgl/BoardConfig.mk
