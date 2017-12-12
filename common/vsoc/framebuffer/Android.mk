# Copyright (C) 2017 The Android Open Source Project
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

include $(CLEAR_VARS)

LOCAL_MODULE := libvsoc_framebuffer
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    ../../../common/vsoc/lib/fb_bcast_layout.cpp \
    fb_bcast_region_view.cpp

LOCAL_C_INCLUDES += \
    device/google/cuttlefish_common/common/vsoc/framebuffer \
    device/google/cuttlefish_common \
    device/google/cuttlefish_kernel \
    system/core/base/include

LOCAL_CFLAGS := \
    -DLOG_TAG=\"libvsoc_framebuffer\" \
    -Wno-missing-field-initializers \
    -Wall -Werror

LOCAL_SHARED_LIBRARIES := \
    libbase \
    libcuttlefish_auto_resources \
    libcuttlefish_fs \
    liblog \
    libvsoc

LOCAL_VENDOR_MODULE := true
# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := test_framebuffer
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := test_fb.cpp

LOCAL_C_INCLUDES += \
    device/google/cuttlefish_common/common/vsoc/framebuffer \
    device/google/cuttlefish_common \
    device/google/cuttlefish_kernel \
    system/core/base/include

LOCAL_CFLAGS := \
    -DLOG_TAG=\"test_framebuffer\" \
    -Wno-missing-field-initializers \
    -Wall -Werror

LOCAL_SHARED_LIBRARIES := \
    libbase \
    libcuttlefish_auto_resources \
    libcuttlefish_fs \
    liblog \
    libvsoc \
    libvsoc_framebuffer

LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)
