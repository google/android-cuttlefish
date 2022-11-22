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

ifeq ($(TARGET_VULKAN_SUPPORT),true)

# ANGLE provides an OpenGL implementation built on top of Vulkan.
PRODUCT_PACKAGES += \
    libEGL_angle \
    libGLESv1_CM_angle \
    libGLESv2_angle \

# ANGLE options:
#
# * preferLinearFilterForYUV
#     Prefer linear filtering for YUV AHBs to pass
#     android.media.decoder.cts.DecodeAccuracyTest.
#
# * mapUnspecifiedColorSpaceToPassThrough
#     Map unspecified color spaces to PASS_THROUGH to pass
#     android.media.codec.cts.DecodeEditEncodeTest and
#     android.media.codec.cts.EncodeDecodeTest.
PRODUCT_VENDOR_PROPERTIES += \
    debug.angle.feature_overrides_enabled=preferLinearFilterForYUV:mapUnspecifiedColorSpaceToPassThrough \

endif
