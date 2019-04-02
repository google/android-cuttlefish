# Copyright (C) 2006 The Android Open Source Project
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

# only for PLATFORM_VERSION greater or equal to Q
ifeq (true,$(ENABLE_CUTTLEFISH_RILD))
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    rild_cuttlefish.c

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libdl \
    liblog \
    libril-cuttlefish-fork

LOCAL_C_INCLUDES += \
    device/google/cuttlefish_common

# Temporary hack for broken vendor RILs.
LOCAL_WHOLE_STATIC_LIBRARIES := \
    librilutils

LOCAL_CFLAGS := -DRIL_SHLIB
LOCAL_CFLAGS += -Wall -Wextra -Werror

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE:= libcuttlefish-rild
LOCAL_OVERRIDES_PACKAGES := rild
PACKAGES.$(LOCAL_MODULE).OVERRIDES := rild
LOCAL_INIT_RC := rild_cuttlefish.rc

include $(BUILD_EXECUTABLE)
endif
