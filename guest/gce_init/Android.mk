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

# Android makefile for init processes
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE:= gce_init
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/gce_ramdisk
LOCAL_MODULE_STEM := init

LOCAL_SRC_FILES := \
    environment_setup.cpp \
    properties.cpp \
    gce_init.cpp \

LOCAL_C_INCLUDES := \
    device/google/gce/gce_utils \
    device/google/gce/include \
    system/extras \
    external/zlib \
    $(GCE_STLPORT_INCLUDES)

LOCAL_CFLAGS := -Wall -Werror $(GCE_VERSION_CFLAGS)

LOCAL_STATIC_LIBRARIES := \
    libgcenetwork \
    libgcesys \
    libcurl \
    libssl_static \
    libcrypto_static \
    libramdisk \
    liblog \
    libgcemetadata \
    libgcecutils \
    liblog \
    libjsoncpp \
    libcutils \
    libz \
    $(GCE_STLPORT_STATIC_LIBS) \
    $(GCE_LIBCXX_STATIC) \
    libstdc++ \
    libc \
    libm \
    libdl_stubs

LOCAL_FORCE_STATIC_EXECUTABLE := true

ifeq ($(TARGET_TRANSLATE_2ND_ARCH),true)
# We don't need secondary binary if secondary arch is translated
ifneq (1,$(words $(LOCAL_MODULE_TARGET_ARCH)))
LOCAL_MULTILIB := first
endif
endif
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE:= gce_init_dhcp_hook
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/gce_ramdisk/sbin
LOCAL_SRC_FILES := gce_init_dhcp_hook.cpp
LOCAL_C_INCLUDES := device/google/gce/include
LOCAL_CFLAGS := -Wall -Werror
LOCAL_STATIC_LIBRARIES := libstdc++ libc
LOCAL_FORCE_STATIC_EXECUTABLE := true
ifeq ($(TARGET_TRANSLATE_2ND_ARCH),true)
# We don't need secondary binary if secondary arch is translated
ifneq (1,$(words $(LOCAL_MODULE_TARGET_ARCH)))
LOCAL_MULTILIB := first
endif
endif
include $(BUILD_EXECUTABLE)

###########################################################

ifneq (,$(GCE_TEST_LIBRARIES))
include $(CLEAR_VARS)
LOCAL_MODULE := gce_init_test
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
    properties.cpp \
    properties_test.cpp

LOCAL_C_INCLUDES := $(GCE_TEST_INCLUDES) \
    device/google/gce/include

LOCAL_STATIC_LIBRARIES := $(GCE_TEST_LIBRARIES)
LOCAL_SHARED_LIBRARIES := libc++
LOCAL_CFLAGS := -Wall -Werror -std=c++11 $(GCE_VERSION_CFLAGS)
LOCAL_LDFLAGS := -Wl,-rpath=$(ANDROID_BUILD_TOP)/$(HOST_OUT_SHARED_LIBRARIES)
include $(BUILD_HOST_EXECUTABLE)

endif


