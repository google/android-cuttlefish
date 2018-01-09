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

include $(CLEAR_VARS)

# Emulator camera module########################################################

emulator_camera_module_relative_path := hw
emulator_camera_cflags := -fno-short-enums $(VSOC_VERSION_CFLAGS)
emulator_camera_cflags += \
    -std=gnu++11 \
    -Wall \
    -Werror

emulator_camera_shared_libraries := \
    libbase \
    libbinder \
    liblog \
    libutils \
    libcutils \
    libui \
    libdl \
    libjpeg \
    libcamera_metadata \
    libhardware

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -le 22; echo $$?))
emulator_camera_shared_libraries += libjsoncpp
else
emulator_camera_static_libraries += libjsoncpp
endif

emulator_camera_static_libraries += android.hardware.camera.common@1.0-helper

emulator_camera_c_includes := \
    device/google/cuttlefish_common \
    frameworks/native/include/media/hardware \
    $(call include-path-for, camera) \

emulator_camera_src := \
	CameraConfiguration.cpp \
	EmulatedCameraHal.cpp \
	EmulatedCameraFactory.cpp \
	EmulatedBaseCamera.cpp \
	EmulatedCamera.cpp \
		EmulatedCameraDevice.cpp \
		EmulatedFakeCamera.cpp \
		EmulatedFakeCameraDevice.cpp \
		Converters.cpp \
		PreviewWindow.cpp \
		CallbackNotifier.cpp \
		JpegCompressor.cpp
emulated_camera2_src := \
	VSoCEmulatedCameraHotplugThread.cpp \
	EmulatedCamera2.cpp \
		EmulatedFakeCamera2.cpp \
		fake-pipeline2/Scene.cpp \
		fake-pipeline2/Sensor.cpp \
		fake-pipeline2/JpegCompressor.cpp
emulated_camera3_src := \
	EmulatedCamera3.cpp \
		EmulatedFakeCamera3.cpp


emulated_camera2_stub_src := \
	StubEmulatedCamera2.cpp \
		StubEmulatedFakeCamera2.cpp

enable_emulated_camera3 = $(shell test $(PLATFORM_SDK_VERSION) -ge 23 && echo yes)
enable_emulated_camera2 = $(shell test $(PLATFORM_SDK_VERSION) -ge 19 && echo yes)

# Emulated camera - goldfish / vbox_x86 build###################################
#
ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?))
emulator_camera_c_includes += external/libjpeg-turbo
else
emulator_camera_c_includes += external/jpeg
endif

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := ${emulator_camera_module_relative_path}
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/${emulator_camera_module_relative_path}
endif
LOCAL_MULTILIB := first
LOCAL_CFLAGS := ${emulator_camera_cflags}
LOCAL_CLANG_CFLAGS += ${emulator_camera_clang_flags}

LOCAL_SHARED_LIBRARIES := ${emulator_camera_shared_libraries}
LOCAL_STATIC_LIBRARIES := ${emulator_camera_static_libraries}
LOCAL_C_INCLUDES += ${emulator_camera_c_includes}
LOCAL_SRC_FILES := ${emulator_camera_src} ${emulator_camera_ext_src} \
	$(if $(enable_emulated_camera2),$(emulated_camera2_src),) \
	$(if $(enable_emulated_camera3),$(emulated_camera3_src),)

LOCAL_MODULE := camera.vsoc
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

# JPEG stub#####################################################################

include $(CLEAR_VARS)

jpeg_module_relative_path := hw
jpeg_cflags := \
    -fno-short-enums \
    -Wall \
    -Werror

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -lt 27 ; echo $$?))
GCE_HWUI_LIB:=libskia
GCE_HWUI_INCLUDE:=external/skia/include/core
else ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -eq 27 -a $(PLATFORM_PREVIEW_SDK_VERSION) -eq 0; echo $$?))
GCE_HWUI_LIB:=libskia
GCE_HWUI_INCLUDE:=external/skia/include/core
else
GCE_HWUI_LIB:=libhwui
GCE_HWUI_INCLUDE:=
endif

jpeg_shared_libraries := \
    libcutils \
    liblog \
    $(GCE_HWUI_LIB) \
    libjpeg \
    libandroid_runtime \
    cuttlefish_auto_resources
jpeg_static_libraries := libyuv_static
jpeg_c_includes := \
    device/google/cuttlefish_common \
    external/libyuv/files/include \
    $(GCE_HWUI_INCLUDE) \
    frameworks/base/core/jni/android/graphics \
    frameworks/native/include
jpeg_src := \
    JpegStub.cpp \
    ExifMetadataBuilder.cpp

# JPEG stub - goldfish build####################################################

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?))
jpeg_c_includes += external/libjpeg-turbo
else
jpeg_c_includes += external/jpeg
endif

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 21; echo $$?))
LOCAL_MODULE_RELATIVE_PATH := ${emulator_camera_module_relative_path}
else
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/${emulator_camera_module_relative_path}
endif
LOCAL_MULTILIB := first
LOCAL_CFLAGS += ${jpeg_cflags}
LOCAL_CLANG_CFLAGS += ${jpeg_clangflags}

LOCAL_SHARED_LIBRARIES := ${jpeg_shared_libraries}
LOCAL_STATIC_LIBRARIES := ${jpeg_static_libraries}
LOCAL_C_INCLUDES += ${jpeg_c_includes}
LOCAL_SRC_FILES := ${jpeg_src}

LOCAL_MODULE := camera.vsoc.jpeg
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

