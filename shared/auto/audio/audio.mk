#
# Copyright (C) 2025 The Android Open Source Project
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
# AudioService
#
PRODUCT_PRODUCT_PROPERTIES += \
    ro.config.vc_call_vol_default=16 \
    ro.config.media_vol_default=16 \
    ro.config.alarm_vol_default=16 \
    ro.config.system_vol_default=16 \

#
# AudioPolicy
#
BOARD_SEPOLICY_DIRS += frameworks/av/services/audiopolicy/engineconfigurable/sepolicy

PRODUCT_PACKAGES += audio_policy_configuration.xml

# Tool used for debug Parameter Framework (only for eng and userdebug builds)
PRODUCT_PACKAGES_DEBUG += remote-process

#
# AudioPolicyEngine
#
PRODUCT_PACKAGES += audio_policy_engine_configuration.xml

#
# Audio HAL / AudioEffect HAL
#
PRODUCT_PACKAGES += audio_effects_config.xml

#
# CarService
#
PRODUCT_PACKAGES += car_audio_configuration.xml
