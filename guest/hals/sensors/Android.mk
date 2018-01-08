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

# HAL module implemenation stored in
# hw/<SENSORS_HARDWARE_MODULE_ID>.<ro.hardware>.so
include $(CLEAR_VARS)

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := hw
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
endif
LOCAL_MULTILIB := first
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
    $(VSOC_STLPORT_LIBS) \
    libcuttlefish_fs \
    cuttlefish_auto_resources \
    liblog \
    libcutils

LOCAL_HEADER_LIBRARIES := \
    libhardware_headers

LOCAL_SRC_FILES := \
    sensors.cpp \
    sensors_hal.cpp \
    vsoc_sensors.cpp \
    vsoc_sensors_message.cpp

LOCAL_CFLAGS := -DLOG_TAG=\"VSoC-Sensors\" \
    $(VSOC_VERSION_CFLAGS) \
    -Werror -Wall -Wno-missing-field-initializers -Wno-unused-parameter

LOCAL_C_INCLUDES := \
    $(VSOC_STLPORT_INCLUDES) \
    device/google/cuttlefish_common \
    system/extras

LOCAL_STATIC_LIBRARIES := \
    libcutils \
    libcuttlefish_remoter_framework \
    $(VSOC_STLPORT_STATIC_LIBS)

LOCAL_MODULE := sensors.vsoc
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)
