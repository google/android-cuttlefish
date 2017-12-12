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
LOCAL_MODULE := libvsoc
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    ../../../common/vsoc/lib/e2e_test_region_layout.cpp \
    ../../../common/vsoc/lib/lock_common.cpp \
    ../../../common/vsoc/lib/region_view.cpp \
    guest_lock.cpp \
    region_control.cpp \
    region_view.cpp

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common \
    device/google/cuttlefish_kernel \
    system/core/base/include

LOCAL_EXPORT_C_INCLUDES_DIR := \
    device/google/cuttlefish_common  \
    device/google/cuttlefish_kernel

LOCAL_SHARED_LIBRARIES := \
    libcuttlefish_auto_resources \
    libcuttlefish_fs \
    libbase \
    liblog

LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := vsoc_guest_region_e2e_test
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := guest_region_e2e_test.cpp

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common \
    device/google/cuttlefish_kernel

LOCAL_CFLAGS += -DGTEST_OS_LINUX_ANDROID -DGTEST_HAS_STD_STRING

LOCAL_STATIC_LIBRARIES := \
    libgtest

LOCAL_SHARED_LIBRARIES := \
    libvsoc \
    libcuttlefish_auto_resources \
    libcuttlefish_fs \
    libbase

LOCAL_MULTILIB := first
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)
