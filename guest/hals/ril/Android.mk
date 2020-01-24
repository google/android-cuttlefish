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
  cuttlefish_ril.cpp

LOCAL_SHARED_LIBRARIES := \
  liblog \
  libcutils \
  libutils \
  ${CUTTLEFISH_LIBRIL_NAME} \
  libcuttlefish_fs \
  cuttlefish_net \
  libbase \
  libcuttlefish_device_config \

LOCAL_C_INCLUDES := \
    device/google/cuttlefish

LOCAL_CFLAGS += \
  -Wall \
  -Werror \
  $(VSOC_VERSION_CFLAGS)

LOCAL_MODULE:= libcuttlefish-ril
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

# See b/67109557
ifeq (true, $(TARGET_TRANSLATE_2ND_ARCH))
LOCAL_MULTILIB := first
endif

include $(BUILD_SHARED_LIBRARY)

include $(call first-makefiles-under,$(LOCAL_PATH))
