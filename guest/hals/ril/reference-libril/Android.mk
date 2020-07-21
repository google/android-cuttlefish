# Copyright (C) 2019 The Android Open Source Project
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

# We're forced to use Android.mk here because:
#   This depends on headers in hardware/ril/libril
#   hardware/ril/libril is still on Android.mk

ifeq (libril-modem-lib,$(CUTTLEFISH_LIBRIL_NAME))
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_VENDOR_MODULE := true
LOCAL_MODULE := libril-modem-lib
LOCAL_SRC_FILES:= \
    ril.cpp \
    ril_service.cpp \
    ril_event.cpp \
    RilSapSocket.cpp \
    sap_service.cpp \


LOCAL_SHARED_LIBRARIES := \
    liblog \
    libutils \
    libcutils \
    libhardware_legacy \
    libhidlbase \
    librilutils \
    android.hardware.radio@1.0 \
    android.hardware.radio@1.1 \
    android.hardware.radio.deprecated@1.0 \
    android.hardware.radio@1.2 \
    android.hardware.radio@1.3 \
    android.hardware.radio@1.4 \
    android.hardware.radio@1.5 \

LOCAL_STATIC_LIBRARIES := \
    libprotobuf-c-nano-enable_malloc \

LOCAL_C_INCLUDES += \
    device/google/cuttlefish \
    hardware/include \
    external/nanopb-c \
    hardware/ril/include \
    hardware/ril/libril

LOCAL_CFLAGS += \
    -Wextra \
    -Wno-unused-parameter

LOCAL_EXPORT_C_INCLUDE_DIRS := hardware/ril/include

include $(BUILD_SHARED_LIBRARY)
endif
