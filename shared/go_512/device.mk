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

$(call inherit-product, build/target/product/go_defaults_512.mk)
$(call inherit-product, device/google/cuttlefish/shared/phone/device.mk)

# By default, enable zram; experiment can toggle the flag,
# which takes effect on boot
PRODUCT_PROPERTY_OVERRIDES += \
    pm.dexopt.downgrade_after_inactive_days=10 \
    pm.dexopt.shared=quicken \
    dalvik.vm.heapgrowthlimit=128m \
    dalvik.vm.heapsize=256m \
