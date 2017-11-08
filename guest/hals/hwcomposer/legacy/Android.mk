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

# HAL module implemenation stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so

# Old hwcomposer, relies on GLES composition
include $(CLEAR_VARS)
include $(LOCAL_PATH)/hwcomposer.mk
LOCAL_CFLAGS += -DUSE_OLD_HWCOMPOSER
LOCAL_MODULE := hwcomposer.gce_x86-deprecated
include $(BUILD_SHARED_LIBRARY)

# New hwcomposer, performs software composition
include $(CLEAR_VARS)
include $(LOCAL_PATH)/hwcomposer.mk
LOCAL_MODULE := hwcomposer.gce_x86
LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)

# An executable to run some tests
include $(CLEAR_VARS)

LOCAL_MODULE := hwc_tests.gce_x86
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
    libgceframebuffer \
    liblog \
    libcutils \
    libutils \
    libsync \
    libhardware \
    libjpeg \
    $(GCE_STLPORT_LIBS)

LOCAL_STATIC_LIBRARIES := \
    libgcemetadata \
    libyuv_static

LOCAL_SRC_FILES := \
    hwc_tests.cpp \
    gce_composer.cpp \
    base_composer.cpp \
    geometry_utils.cpp


LOCAL_CFLAGS += \
    -DLOG_TAG=\"hwc_tests\" \
    $(GCE_VERSION_CFLAGS)

LOCAL_C_INCLUDES := \
    device/google/gce/hwcomposer \
    device/google/gce/include \
    bionic \
    $(GCE_STLPORT_INCLUDES)

include device/google/gce/libs/base/libbase.mk
LOCAL_SHARED_LIBRARIES += $(GCE_LIBBASE_LIB_NAME)
LOCAL_C_INCLUDES += $(GCE_LIBBASE_INCLUDE_DIR)

include $(BUILD_EXECUTABLE)
