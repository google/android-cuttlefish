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

include $(CLEAR_VARS)

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := hw
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
endif
LOCAL_MODULE_TAGS := optional
LOCAL_MULTILIB := first

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils

LOCAL_SRC_FILES := \
    vsoc_audio_policy_hal.cpp

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common \
    $(VSOC_STLPORT_INCLUDES) \
    frameworks/native/include/media/hardware \
    $(call include-path-for, audio)

LOCAL_CFLAGS := -Wall -Werror

LOCAL_MODULE := audio_policy.vsoc
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)
