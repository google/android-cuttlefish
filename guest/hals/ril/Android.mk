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

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
  vsoc_ril.cpp

LOCAL_SHARED_LIBRARIES := \
  liblog \
  libcutils \
  libutils \
  libcuttlefish_fs \
  cuttlefish_net \
  cuttlefish_auto_resources \
  libbase \
  vsoc_lib

LOCAL_C_INCLUDES := \
    device/google/cuttlefish_common \
    device/google/cuttlefish_kernel

LOCAL_CFLAGS += \
  -Wall \
  -Werror \
  $(VSOC_VERSION_CFLAGS)

# only for PLATFORM_VERSION greater or equal to Q
ifeq ($(PLATFORM_VERSION), $(word 1, $(sort Q $(PLATFORM_VERSION))))

    LOCAL_SRC_FILES += \
        libril/ril.cpp \
        libril/ril_service.cpp \
        libril/ril_event.cpp \
        libril/RilSapSocket.cpp \
        libril/sap_service.cpp

    LOCAL_SHARED_LIBRARIES += \
        libhardware_legacy \
        libhidlbase  \
        libhidltransport \
        libhwbinder \
        librilutils \
        android.hardware.radio@1.0 \
        android.hardware.radio@1.1 \
        android.hardware.radio.deprecated@1.0 \
        android.hardware.radio@1.2 \
        android.hardware.radio@1.3 \
        android.hardware.radio@1.4


    LOCAL_STATIC_LIBRARIES := \
        libprotobuf-c-nano-enable_malloc \

    LOCAL_C_INCLUDES += \
        external/nanopb-c \
        hardware/ril/include \
        hardware/ril/libril

    LOCAL_CFLAGS += \
        -Wextra \
        -Wno-unused-parameter

    LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

    ifeq ($(SIM_COUNT), 2)
        LOCAL_CFLAGS += -DANDROID_MULTI_SIM -DDSDA_RILD1
        LOCAL_CFLAGS += -DANDROID_SIM_COUNT_2
    endif

    ifneq ($(DISABLE_RILD_OEM_HOOK),)
        LOCAL_CFLAGS += -DOEM_HOOK_DISABLED
    endif
else
    $(info Use deprecated libril)
    LOCAL_SHARED_LIBRARIES += \
        libril
endif

LOCAL_MODULE:= libvsoc-ril
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif

include $(BUILD_SHARED_LIBRARY)
