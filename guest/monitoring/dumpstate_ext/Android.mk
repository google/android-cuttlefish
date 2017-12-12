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

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common
LOCAL_CFLAGS := $(VSOC_VERSION_CFLAGS) -DLOG_TAG=\"VSoC-dumpstate\"
LOCAL_SRC_FILES := dumpstate.cpp
LOCAL_MODULE := libdumpstate.vsoc
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := \
    libbase \
    libdumpstateaidl \
    libdumpstateutil \
    libziparchive \
    libz
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := android.hardware.dumpstate@1.0-service.cuttlefish
LOCAL_INIT_RC := android.hardware.dumpstate@1.0-service.cuttlefish.rc
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := \
    dumpstate_device.cpp \
    service.cpp
LOCAL_CFLAGS := $(VSOC_VERSION_CFLAGS) -DLOG_TAG=\"VSoC-dumpstate\"
LOCAL_SHARED_LIBRARIES := \
    android.hardware.dumpstate@1.0 \
    libbase \
    libcutils \
    libdumpstateutil \
    libhidlbase \
    libhidltransport \
    libhwbinder \
    liblog \
    libutils
LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common \
    frameworks/native/cmds/dumpstate

LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)
