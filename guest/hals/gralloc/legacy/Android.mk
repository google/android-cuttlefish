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

# Temporary, should be removed once vsoc hals are in usable state

LOCAL_PATH := $(call my-dir)

VSOC_GRALLOC_COMMON_SRC_FILES := \
    gralloc.cpp \
    mapper.cpp \
    region_registry.cpp

VSOC_GRALLOC_COMMON_CFLAGS:= \
    -DLOG_TAG=\"gralloc_vsoc_legacy\" \
    -Wno-missing-field-initializers \
    -Wall -Werror \
    $(VSOC_VERSION_CFLAGS)

include $(CLEAR_VARS)
LOCAL_MODULE := gralloc.cutf_ashmem
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(VSOC_GRALLOC_COMMON_SRC_FILES)

LOCAL_CFLAGS := $(VSOC_GRALLOC_COMMON_CFLAGS)
LOCAL_C_INCLUDES := \
    device/google/cuttlefish

LOCAL_HEADER_LIBRARIES := \
    libhardware_headers

LOCAL_SHARED_LIBRARIES := \
    libbase \
    liblog \
    libutils \
    libcutils

LOCAL_VENDOR_MODULE := true

# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif

include $(BUILD_SHARED_LIBRARY)
