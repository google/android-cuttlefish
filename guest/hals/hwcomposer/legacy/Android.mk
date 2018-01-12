# Copyright (C) 2016 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

# HAL module implemenation stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so

# Old hwcomposer, relies on GLES composition
include $(CLEAR_VARS)
include $(LOCAL_PATH)/hwcomposer.mk
LOCAL_CFLAGS += -DUSE_OLD_HWCOMPOSER -Wall -Werror
LOCAL_MODULE := hwcomposer.vsoc-deprecated

# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif

include $(BUILD_SHARED_LIBRARY)

# New hwcomposer, performs software composition
include $(CLEAR_VARS)
include $(LOCAL_PATH)/hwcomposer.mk
LOCAL_MODULE := hwcomposer.vsoc
LOCAL_VENDOR_MODULE := true

# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif

include $(BUILD_SHARED_LIBRARY)

# An executable to run some tests
include $(CLEAR_VARS)

LOCAL_MODULE := hwc_tests.vsoc
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
    libvsocframebuffer \
    liblog \
    libbase \
    libcutils \
    libutils \
    libsync \
    libhardware \
    libjpeg \
    $(VSOC_STLPORT_LIBS)

LOCAL_STATIC_LIBRARIES := \
    libyuv_static

LOCAL_SRC_FILES := \
    hwc_tests.cpp \
    vsoc_composer.cpp \
    base_composer.cpp \
    geometry_utils.cpp


LOCAL_CFLAGS += \
    $(VSOC_VERSION_CFLAGS) \
    -Wall -Werror

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common \
    bionic \
    $(VSOC_STLPORT_INCLUDES)

include $(BUILD_EXECUTABLE)
