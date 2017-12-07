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
# x86_64  target for Cuttlefish
#

-include device/google/cuttlefish/shared/BoardConfig.mk

TARGET_ARCH := x86_64
TARGET_ARCH_VARIANT := silvermont
TARGET_CPU_ABI := x86_64
TARGET_CPU_ABI_LIST_32_BIT := x86
TARGET_2ND_ARCH := x86
TARGET_2ND_CPU_ABI := x86
TARGET_2ND_CPU_ABI2 := x86
TARGET_2ND_ARCH_VARIANT := silvermont
TARGET_2ND_CPU_VARIANT := silvermont

