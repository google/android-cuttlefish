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

GCE_GRALLOC_COMMON_SRC_FILES := \
    gralloc.cpp \
    framebuffer.cpp \
    mapper.cpp

GCE_GRALLOC_COMMON_CFLAGS:= \
    -DLOG_TAG=\"gralloc_gce_x86\" \
    -Wno-missing-field-initializers \
    -Wall -Werror \
    $(GCE_VERSION_CFLAGS)

GCE_GRALLOC_COMMON_C_INCLUDES := \
    device/google/gce/include

GCE_GRALLOC_COMMON_SHARED_LIBRARIES := \
    liblog \
    libutils \
    libcutils \
    libgceframebuffer

GCE_GRALLOC_COMMON_STATIC_LIBRARIES := \
    libgcemetadata


include $(CLEAR_VARS)
LOCAL_MODULE := gralloc.gce_x86
ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := hw
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
endif
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(GCE_GRALLOC_COMMON_SRC_FILES)

LOCAL_CFLAGS := $(GCE_GRALLOC_COMMON_CFLAGS)
LOCAL_C_INCLUDES := $(GCE_GRALLOC_COMMON_C_INCLUDES)
LOCAL_SHARED_LIBRARIES := $(GCE_GRALLOC_COMMON_SHARED_LIBRARIES)
LOCAL_STATIC_LIBRARIES := $(GCE_GRALLOC_COMMON_STATIC_LIBRARIES)
LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)

