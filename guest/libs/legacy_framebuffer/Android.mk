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

# Temporary, this library should go away once the new HALS are in place.

vsocframebuffer_common_src_files := \
    vsoc_framebuffer.cpp \
    vsoc_framebuffer_control.cpp \
    RegionRegistry.cpp

vsocframebuffer_common_c_flags := -Wall -Werror $(VSOC_VERSION_CFLAGS)

vsocframebuffer_common_c_includes := \
    device/google/cuttlefish_common \
    device/google/cuttlefish_kernel

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libvsocframebuffer
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := ${vsocframebuffer_common_src_files}
LOCAL_CFLAGS += ${vsocframebuffer_common_c_flags}
LOCAL_C_INCLUDES := ${vsocframebuffer_common_c_includes} \
    $(VSOC_STLPORT_INCLUDES)

LOCAL_STATIC_LIBRARIES := \
    libjsoncpp

LOCAL_SHARED_LIBRARIES := \
    libbase \
    liblog \
    libutils \
    libcutils \
    libcuttlefish_auto_resources \
    libcuttlefish_fs \
    libvsoc \
    libvsoc_framebuffer \
    $(VSOC_STLPORT_LIBS)

include $(BUILD_SHARED_LIBRARY)
