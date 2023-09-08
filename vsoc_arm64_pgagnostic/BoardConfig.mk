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

-include device/google/cuttlefish/vsoc_arm64_only/BoardConfig.mk

TARGET_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_RO_FILE_SYSTEM_TYPE := ext4

