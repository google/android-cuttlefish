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

# If a downstream target does not want any graphics support, do not
# include this file!

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/graphics/init_graphics.vendor.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init_graphics.vendor.rc \

# Gfxstream common libraries:
PRODUCT_SOONG_NAMESPACES += device/generic/goldfish-opengl
PRODUCT_PACKAGES += \
    libandroidemu \
    libOpenglCodecCommon \
    libOpenglSystemCommon \
    libGLESv1_CM_emulation \
    lib_renderControl_enc \
    libEGL_emulation \
    libGLESv2_enc \
    libGLESv2_emulation \
    libGLESv1_enc \
    libGoldfishProfiler \

# Gfxstream OpenGL implementation (OpenGL streamed to the host).
PRODUCT_PACKAGES += \
    libEGL_emulation \
    libGLESv1_CM_emulation \
    libGLESv1_enc \
    libGLESv2_emulation \
    libGLESv2_enc \

# Gfxstream Vulkan implementation (Vulkan streamed to the host).
ifeq ($(TARGET_VULKAN_SUPPORT),true)
PRODUCT_PACKAGES += com.google.cf.vulkan
PRODUCT_VENDOR_PROPERTIES += ro.vulkan.apex=com.google.cf.vulkan
endif

#
# Hardware Composer HAL
#
PRODUCT_PACKAGES += \
    com.android.hardware.graphics.composer.ranchu

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.hwcomposer.pmem=/dev/block/pmem1

# Gralloc implementation
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator-service.minigbm \
    android.hardware.graphics.mapper@4.0-impl.minigbm \
    mapper.minigbm
