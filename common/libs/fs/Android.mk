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
LOCAL_MODULE := libcuttlefish_fs
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    gce_fs.cpp \
    shared_fd.cpp

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common

LOCAL_SHARED_LIBRARIES := \
    libcuttlefish_auto_resources \
    libbase

LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)
