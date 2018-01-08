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

# HAL module implemenation, not prelinked and stored in
# hw/<LIGHTS_HARDWARE_MODULE_ID>.<ro.hardware>.so
include $(CLEAR_VARS)

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := hw
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
endif
LOCAL_MULTILIB := first
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_SRC_FILES := gps_vsoc.cpp gps_thread.cpp
LOCAL_MODULE := gps.vsoc
LOCAL_C_INCLUDES := device/google/cuttlefish_common

LOCAL_HEADER_LIBRARIES := \
    libhardware_headers

LOCAL_CFLAGS := \
    -Wall -Werror -Wno-missing-field-initializers \
    -DLOG_TAG=\"VSoCGPS\" \
    $(VSOC_VERSION_CFLAGS)
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

