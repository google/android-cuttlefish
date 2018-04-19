# Copyright (C) 2018 The Android Open Source Project
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

# These modules use the _a name to ensure they are listed first in the
# directory they are installed into so they are picked up before other
# drivers.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libEGL_a_wrapper
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
    egl.cpp \
    egl_wrapper_context.cpp \
    egl_wrapper_entry.cpp
LOCAL_CFLAGS := -Wall -Werror
LOCAL_C_INCLUDES := frameworks/native/opengl/include
LOCAL_SHARED_LIBRARIES := libdl

LOCAL_VENDOR_MODULE := true

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := egl
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/egl
endif

# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libGLESv1_CM_a_wrapper
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
    gles1.cpp \
    gles1_wrapper_context.cpp \
    gles1_wrapper_entry.cpp
LOCAL_CFLAGS := -Wall -Werror
LOCAL_C_INCLUDES := frameworks/native/opengl/include
LOCAL_SHARED_LIBRARIES := libdl

LOCAL_VENDOR_MODULE := true

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := egl
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/egl
endif

# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libGLESv2_a_wrapper
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
    gles3.cpp \
    gles3_wrapper_context.cpp \
    gles3_wrapper_entry.cpp
LOCAL_CFLAGS := -Wall -Werror
LOCAL_C_INCLUDES := frameworks/native/opengl/include
LOCAL_SHARED_LIBRARIES := libdl

LOCAL_VENDOR_MODULE := true

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := egl
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/egl
endif

# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif

include $(BUILD_SHARED_LIBRARY)
