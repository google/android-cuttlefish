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

# Android makefile for GCE Network

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libgcenetwork
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
    dhcp_message.cpp \
    dhcp_server.cpp \
    metadata_proxy.cpp \
    namespace_aware_executor.cpp \
    netlink_client.cpp \
    network_interface_manager.cpp \
    network_namespace_manager.cpp \
    serializable.cpp \

LOCAL_C_INCLUDES := \
    device/google/gce/include \
    external/curl/include \
    $(GCE_STLPORT_INCLUDES)

LOCAL_STATIC_LIBRARIES := libjsoncpp

LOCAL_CFLAGS := -Wall -Werror $(GCE_VERSION_CFLAGS)
include $(BUILD_STATIC_LIBRARY)

###########################################################

include $(CLEAR_VARS)
LOCAL_MODULE := libgcesys
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
    sys_client.cpp

LOCAL_C_INCLUDES := \
    device/google/gce/include \
    external/curl/include \
    $(GCE_STLPORT_INCLUDES)

LOCAL_CFLAGS := -Wall -Werror $(GCE_VERSION_CFLAGS)

include $(BUILD_STATIC_LIBRARY)

###########################################################

ifneq (,$(GCE_TEST_LIBRARIES))
include $(CLEAR_VARS)

LOCAL_MODULE := test_gce_network
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := \
    netlink_client.cpp \
    netlink_client_test.cpp
LOCAL_C_INCLUDES := $(GCE_TEST_INCLUDES) \
    device/google/gce/include
LOCAL_STATIC_LIBRARIES := $(GCE_TEST_LIBRARIES)
LOCAL_SHARED_LIBRARIES := libc++
LOCAL_CFLAGS := -Wall -Werror -std=c++11 $(GCE_VERSION_CFLAGS)
LOCAL_LDFLAGS := -Wl,-rpath=$(HOST_OUT_SHARED_LIBRARIES)
include $(BUILD_HOST_EXECUTABLE)

.PHONY: gce_network_test
gce_network_test: $(LOCAL_INSTALLED_MODULE)
	$<

endif

###########################################################

include $(CLEAR_VARS)
LOCAL_MODULE := gce_network
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    main.cpp \

LOCAL_C_INCLUDES := \
    device/google/gce/include \
    $(GCE_STLPORT_INCLUDES)

LOCAL_CFLAGS := -Wall -Werror $(GCE_VERSION_CFLAGS)

LOCAL_STATIC_LIBRARIES := \
    libgcenetwork \
    libgcesys \
    libgcecutils \
    libgcemetadata \
    libstdc++ \
    libc \
    libm \
    libcurl \
    libssl_static \
    libcrypto_static \
    libz \
    libcutils \
    liblog \
    $(GCE_STLPORT_STATIC_LIBS) \
    libdl_stubs

LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)

###########################################################

include $(CLEAR_VARS)

# This is to generate wpa_supplicant.conf using the target product name and
# model as that file requires such build target specific fields.

LOCAL_MODULE := wpa_supplicant.gce_x86.conf
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/wifi
LOCAL_MODULE_STEM := wpa_supplicant.conf

ifeq ($(TARGET_TRANSLATE_2ND_ARCH),true)
# We don't need secondary binary if secondary arch is translated
ifneq (1,$(words $(LOCAL_MODULE_TARGET_ARCH)))
LOCAL_MULTILIB := first
endif
endif

include $(BUILD_SYSTEM)/base_rules.mk

$(LOCAL_BUILT_MODULE):
	$(hide) echo "Generating $@"
	@mkdir -p $(dir $@)
	$(hide) device/google/gce/gce_utils/gce_network/generate_wpa_supplicant_conf.sh \
	    "${TARGET_PRODUCT}" "${PRODUCT_MODEL}" "${PLATFORM_SDK_VERSION}" \
	    > $@
