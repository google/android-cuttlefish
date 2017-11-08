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

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MULTILIB := first
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
    geometry_utils.cpp \
    hwcomposer.cpp \
    gce_composer.cpp \
    stats_keeper.cpp \
    base_composer.cpp

LOCAL_CFLAGS += \
    -DLOG_TAG=\"hwcomposer\" \
    -DGATHER_STATS \
    $(GCE_VERSION_CFLAGS)

LOCAL_C_INCLUDES := \
    device/google/gce/hwcomposer \
    device/google/gce/include \
    external/libyuv/files/include \
    bionic \
    $(GCE_STLPORT_INCLUDES)

include device/google/gce/libs/base/libbase.mk
LOCAL_SHARED_LIBRARIES += $(GCE_LIBBASE_LIB_NAME)
LOCAL_C_INCLUDES += $(GCE_LIBBASE_INCLUDE_DIR)
