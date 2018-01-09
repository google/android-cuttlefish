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
LOCAL_MODULE := usbforward
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
    main.cpp \
    usb_server.cpp \
    transport_request.cpp

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common \
    device/google/cuttlefish_kernel

LOCAL_HEADER_LIBRARIES := \
    libcutils_headers

LOCAL_SHARED_LIBRARIES := \
    cuttlefish_auto_resources \
    libcuttlefish_fs \
    libusb \
    libbase \
    liblog

LOCAL_CFLAGS += -DLOG_TAG=\"UsbForward\"

LOCAL_MULTILIB := first
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)

