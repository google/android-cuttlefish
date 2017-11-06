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
#
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)


ifneq (,$(filter $(PLATFORM_SDK_VERSION),16 17 18 19 21 22 23)) # J K L M
  # prior to (not including) nyc, libjpeg was used instead of libjpeg turbo
  # the libjpeg turbo in external/ is a backport with the shared library name
  # changed to libjpeg_turbo to avoid conflict with the system's libjpeg
  LIBJPEG_TURBO_NAME := libjpeg_turbo
else
  # nyc and later use libjpeg turbo under its usual name
  LIBJPEG_TURBO_NAME := libjpeg
endif

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common \
    external/libjpeg-turbo \
    external/jsoncpp/include

LOCAL_MODULE := vnc_server
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
	blackboard.cpp \
	frame_buffer_watcher.cpp \
	jpeg_compressor.cpp \
	main.cpp \
	simulated_hw_composer.cpp \
	tcp_socket.cpp \
	VirtualInputDevice.cpp \
	virtual_inputs.cpp \
	vnc_client_connection.cpp \
	vnc_server.cpp \

LOCAL_CFLAGS := \
	$(VSOC_VERSION_CFLAGS) \
	-std=gnu++11 \
	-Wall -Werror \
	-Wno-error-unused -Wno-error=unused-parameter \
	-Wno-attributes

LOCAL_CFLAGS += -Wno-error=implicit-exception-spec-mismatch

LOCAL_SHARED_LIBRARIES := \
    $(LIBJPEG_TURBO_NAME) \
    libbase \
    liblog \
    libutils \
    libcutils \
    libcuttlefish_auto_resources \
    libcuttlefish_fs \
    libvsoc \
    libvsocframebuffer

LOCAL_STATIC_LIBRARIES := \
    liblog \
    libjsoncpp

include $(BUILD_EXECUTABLE)
