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
	aosp_cf_x86_64_auto:$(LOCAL_DIR)/vsoc_x86_64/auto/device.mk \
	aosp_cf_x86_64_pasan:$(LOCAL_DIR)/vsoc_x86_64/pasan/device.mk \
	aosp_cf_x86_64_phone:$(LOCAL_DIR)/vsoc_x86_64/phone/device.mk \
	aosp_cf_x86_64_tablet:$(LOCAL_DIR)/vsoc_x86_64/tablet/device.mk \
	aosp_cf_x86_64_tablet_3g:$(LOCAL_DIR)/vsoc_x86_64/tablet_3g/device.mk \
	aosp_cf_x86_64_tv:$(LOCAL_DIR)/vsoc_x86_64/tv/device.mk \
	aosp_cf_x86_64_wear:$(LOCAL_DIR)/vsoc_x86_64/wear/device.mk \
	aosp_cf_x86_auto:$(LOCAL_DIR)/vsoc_x86/auto/device.mk \
	aosp_cf_x86_iot:$(LOCAL_DIR)/vsoc_x86/iot/device.mk \
	aosp_cf_x86_pasan:$(LOCAL_DIR)/vsoc_x86/pasan/device.mk \
	aosp_cf_x86_phone:$(LOCAL_DIR)/vsoc_x86/phone/device.mk \
	aosp_cf_x86_tablet:$(LOCAL_DIR)/vsoc_x86/tablet/device.mk \
	aosp_cf_x86_tablet_3g:$(LOCAL_DIR)/vsoc_x86/tablet_3g/device.mk \
	aosp_cf_x86_tv:$(LOCAL_DIR)/vsoc_x86/tv/device.mk \
	aosp_cf_x86_wear:$(LOCAL_DIR)/vsoc_x86/wear/device.mk \

COMMON_LUNCH_CHOICES := \
	aosp_cf_x86_auto-userdebug \
	aosp_cf_x86_iot-userdebug \
	aosp_cf_x86_phone-userdebug \
	aosp_cf_x86_tablet-userdebug \
	aosp_cf_x86_tablet_3g-userdebug \
	aosp_cf_x86_tv-userdebug \
	aosp_cf_x86_wear-userdebug \
	aosp_cf_x86_64_auto-userdebug \
	aosp_cf_x86_64_phone-userdebug \
	aosp_cf_x86_64_tablet-userdebug \
	aosp_cf_x86_64_tablet_3g-userdebug \
	aosp_cf_x86_64_tv-userdebug \
	aosp_cf_x86_64_wear-userdebug
