#
# Copyright (C) 2018 The Android Open Source Project
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

$(call inherit-product, device/google/cuttlefish/shared/go_512/device.mk)
$(call inherit-product, device/google/cuttlefish/vsoc_x86/device.mk)

PRODUCT_NAME := aosp_cf_x86_go_512_phone
PRODUCT_DEVICE := vsoc_x86
PRODUCT_MODEL := Cuttlefish x86 Go 512 phone
PRODUCT_PACKAGE_OVERLAYS := device/google/cuttlefish/vsoc_x86/phone/overlay
