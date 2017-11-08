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

gceframebuffer_common_src_files := \
    GceFrameBuffer.cpp \
    GceFrameBufferControl.cpp \
    RegionRegistry.cpp

gceframebuffer_common_c_flags := -Wall -Werror $(GCE_VERSION_CFLAGS)

gceframebuffer_common_c_includes := \
    device/google/gce/include

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libgceframebuffer
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := ${gceframebuffer_common_src_files}
LOCAL_CFLAGS += ${gceframebuffer_common_c_flags}
LOCAL_C_INCLUDES := ${gceframebuffer_common_c_includes} \
    $(GCE_STLPORT_INCLUDES)

LOCAL_STATIC_LIBRARIES := \
    libgcemetadata \
    libgcecutils \
    libjsoncpp

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libutils \
    libcutils \
    $(GCE_STLPORT_LIBS)

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libgceframebuffer
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := ${gceframebuffer_common_src_files}
LOCAL_CFLAGS += ${gceframebuffer_common_c_flags}
LOCAL_C_INCLUDES := ${gceframebuffer_common_c_includes}

LOCAL_STATIC_LIBRARIES := \
    libgcemetadata \
    libjsoncpp \
    liblog \
    libutils \
    libcutils \
    $(GCE_STLPORT_LIBS)

include $(BUILD_STATIC_LIBRARY)

# This is needed only before M
# By M STLport is completely replaces with libcxx
ifneq (,$(filter $(PLATFORM_SDK_VERSION),16 17 18 19 21 22))
include $(CLEAR_VARS)

LOCAL_SRC_FILES := ${gceframebuffer_common_src_files}
LOCAL_CFLAGS += ${gceframebuffer_common_c_flags}
LOCAL_C_INCLUDES := ${gceframebuffer_common_c_includes}

LOCAL_STATIC_LIBRARIES += \
    libgcemetadata_cxx \
    libjsoncpp_cxx \
    libgcecutils_cxx \
    libutils \
    libcutils \
    liblog

LOCAL_MULTILIB := first
LOCAL_MODULE := libgceframebuffer_cxx
LOCAL_MODULE_TAGS := optional
include external/libcxx/libcxx.mk
include $(BUILD_STATIC_LIBRARY)
endif
