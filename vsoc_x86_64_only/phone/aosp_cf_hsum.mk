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

# Inherit mostly from aosp_cf_x86_64_phone
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64_only/phone/aosp_cf.mk)
PRODUCT_NAME := aosp_cf_x86_64_only_phone_hsum
PRODUCT_MODEL := Cuttlefish x86_64 phone 64-bit only Headless System User Mode

# Set Headless System User Mode
PRODUCT_SYSTEM_DEFAULT_PROPERTIES = \
    ro.fw.mu.headless_system_user=true

# TODO(b/204071542): add package allow-list; something like
# PRODUCT_COPY_FILES += \
#    device/google/cuttlefish/SOME_PATH/preinstalled-packages.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/sysconfig/preinstalled-packages-cf_phone.xml
