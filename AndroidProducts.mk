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

PRODUCT_MAKEFILES := \
	aosp_cf_arm64_phone:$(LOCAL_DIR)/vsoc_arm64/phone/aosp_cf.mk \
	aosp_cf_x86_64_phone:$(LOCAL_DIR)/vsoc_x86_64/phone/aosp_cf.mk \
	aosp_cf_x86_auto:$(LOCAL_DIR)/vsoc_x86/auto/device.mk \
	aosp_cf_x86_pasan:$(LOCAL_DIR)/vsoc_x86/pasan/device.mk \
	aosp_cf_x86_phone:$(LOCAL_DIR)/vsoc_x86/phone/aosp_cf.mk \
	aosp_cf_x86_go_phone:$(LOCAL_DIR)/vsoc_x86/go_phone/device.mk \
	aosp_cf_x86_go_512_phone:$(LOCAL_DIR)/vsoc_x86/go_512_phone/device.mk \
	aosp_cf_x86_gsi:$(LOCAL_DIR)/vsoc_x86/gsi/aosp_cf_x86_gsi.mk \
	aosp_cf_x86_tv:$(LOCAL_DIR)/vsoc_x86/tv/device.mk

COMMON_LUNCH_CHOICES := \
	aosp_cf_arm64_phone-userdebug \
	aosp_cf_x86_64_phone-userdebug \
	aosp_cf_x86_auto-userdebug \
	aosp_cf_x86_phone-userdebug \
	aosp_cf_x86_tv-userdebug
