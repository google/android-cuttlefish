# Copyright 2006 The Android Open Source Project

# XXX using libutils for simulator build only...
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    reference-ril.c \
    atchannel.c \
    misc.c \
    at_tok.c

LOCAL_SHARED_LIBRARIES := \
    liblog libcutils libutils ${CUTTLEFISH_LIBRIL_NAME} librilutils

LOCAL_STATIC_LIBRARIES := libqemu_pipe libbase

# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE
LOCAL_CFLAGS += -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Werror

LOCAL_C_INCLUDES := \
    device/google/cuttlefish

ifeq ($(TARGET_DEVICE),sooner)
  LOCAL_CFLAGS += -DUSE_TI_COMMANDS
endif

ifeq ($(TARGET_DEVICE),surf)
  LOCAL_CFLAGS += -DPOLL_CALL_STATE -DUSE_QMI
endif

ifeq ($(TARGET_DEVICE),dream)
  LOCAL_CFLAGS += -DPOLL_CALL_STATE -DUSE_QMI
endif

LOCAL_VENDOR_MODULE:= true

LOCAL_MODULE:= libcuttlefish-ril-2
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

