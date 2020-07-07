#
# Copyright (C) 2017 The Android Open Source Project
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
#

# Include all languages
$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)

# Enable updating of APEXes
$(call inherit-product, $(SRC_TARGET_DIR)/product/updatable_apex.mk)

# Enable userspace reboot
$(call inherit-product, $(SRC_TARGET_DIR)/product/userspace_reboot.mk)

PRODUCT_SHIPPING_API_LEVEL := 30
PRODUCT_BUILD_BOOT_IMAGE := true
PRODUCT_USE_DYNAMIC_PARTITIONS := true
DISABLE_RILD_OEM_HOOK := true

PRODUCT_SOONG_NAMESPACES += device/generic/goldfish-opengl # for vulkan

TARGET_USERDATAIMAGE_FILE_SYSTEM_TYPE ?= f2fs

TARGET_VULKAN_SUPPORT ?= true

AB_OTA_UPDATER := true
AB_OTA_PARTITIONS += \
    odm \
    product \
    system \
    system_ext \
    vbmeta \
    vbmeta_system \
    vendor

# Enable Virtual A/B
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota.mk)

# Enable Scoped Storage related
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

# Properties that are not vendor-specific. These will go in the product
# partition, instead of the vendor partition, and do not need vendor
# sepolicy
PRODUCT_PRODUCT_PROPERTIES += \
    persist.adb.tcp.port=5555 \
    ro.com.google.locationfeatures=1 \

# Explanation of specific properties:
#   debug.hwui.swap_with_damage avoids boot failure on M http://b/25152138
#   ro.opengles.version OpenGLES 3.0
PRODUCT_PROPERTY_OVERRIDES += \
    tombstoned.max_tombstone_count=500 \
    bt.rootcanal_test_console=off \
    debug.hwui.swap_with_damage=0 \
    ro.carrier=unknown \
    ro.com.android.dataroaming=false \
    ro.hardware.virtual_device=1 \
    ro.logd.size=1M \
    ro.opengles.version=196608 \
    wifi.interface=wlan0 \
    persist.sys.zram_enabled=1 \
    ro.rebootescrow.device=/dev/block/pmem0 \
    ro.incremental.enable=1 \

# Below is a list of properties we probably should get rid of.
PRODUCT_PROPERTY_OVERRIDES += \
    wlan.driver.status=ok

# Codec 2.0 is unstable on x86
PRODUCT_PROPERTY_OVERRIDES += \
    debug.stagefright.ccodec=0

# Enforce privapp-permissions whitelist.
PRODUCT_PROPERTY_OVERRIDES += ro.control_privapp_permissions=enforce

# aes-256-heh default is not supported in standard kernels.
PRODUCT_PROPERTY_OVERRIDES += ro.crypto.volume.filenames_mode=aes-256-cts

# Copy preopted files from system_b on first boot
PRODUCT_PROPERTY_OVERRIDES += ro.cp_system_other_odex=1

# DRM service opt-in
PRODUCT_PROPERTY_OVERRIDES += drm.service.enabled=true

PRODUCT_SOONG_NAMESPACES += hardware/google/camera
PRODUCT_SOONG_NAMESPACES += hardware/google/camera/devices/EmulatedCamera

#
# Packages for various GCE-specific utilities
#
PRODUCT_PACKAGES += \
    socket_vsock_proxy \
    CuttlefishService \
    wpa_supplicant.vsoc.conf \
    vsoc_input_service \
    vport_trigger \
    rename_netiface \
    ip_link_add \
    setup_wifi \
    tombstone_transmit \
    vsock_logcat \
    tombstone_producer \
    suspend_blocker \

#
# Packages for AOSP-available stuff we use from the framework
#
PRODUCT_PACKAGES += \
    e2fsck \
    ip \
    sleep \
    tcpdump \
    wpa_supplicant \
    wificond \

#
# Packages for the OpenGL implementation
#

# SwiftShader provides a software-only implementation that is not thread-safe
PRODUCT_PACKAGES += \
    libEGL_swiftshader \
    libGLESv1_CM_swiftshader \
    libGLESv2_swiftshader

# GL implementation for virgl
PRODUCT_PACKAGES += \
    libGLES_mesa \

#
# Packages for the Vulkan implementation
#
ifeq ($(TARGET_VULKAN_SUPPORT),true)
PRODUCT_PACKAGES += \
    vulkan.ranchu \
    libvulkan_enc \
    vulkan.pastel
endif

# GL/Vk implementation for gfxstream
PRODUCT_PACKAGES += \
    libandroidemu \
    libOpenglCodecCommon \
    libOpenglSystemCommon \
    libGLESv1_CM_emulation \
    lib_renderControl_enc \
    libEGL_emulation \
    libGLESv2_enc \
    libGLESv2_emulation \
    libGLESv1_enc

#
# Packages for testing
#
PRODUCT_PACKAGES += \
    aidl_lazy_test_server \
    hidl_lazy_test_server

DEVICE_PACKAGE_OVERLAYS := device/google/cuttlefish/shared/overlay
# PRODUCT_AAPT_CONFIG and PRODUCT_AAPT_PREF_CONFIG are intentionally not set to
# pick up every density resources.

#
# General files
#
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/audio_policy.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy.conf \
    hardware/google/camera/devices/EmulatedCamera/hwl/configs/emu_camera_back.json:$(TARGET_COPY_OUT_VENDOR)/etc/config/emu_camera_back.json \
    hardware/google/camera/devices/EmulatedCamera/hwl/configs/emu_camera_front.json:$(TARGET_COPY_OUT_VENDOR)/etc/config/emu_camera_front.json \
    hardware/google/camera/devices/EmulatedCamera/hwl/configs/emu_camera_depth.json:$(TARGET_COPY_OUT_VENDOR)/etc/config/emu_camera_depth.json \
    device/google/cuttlefish/shared/config/init.vendor.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.cutf_cvm.rc \
    device/google/cuttlefish/shared/config/init.product.rc:$(TARGET_COPY_OUT_PRODUCT)/etc/init/init.rc \
    device/google/cuttlefish/shared/config/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/ueventd.rc \
    device/google/cuttlefish/shared/config/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    device/google/cuttlefish/shared/config/media_codecs_google_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video.xml \
    device/google/cuttlefish/shared/config/media_codecs_performance.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_performance.xml \
    device/google/cuttlefish/shared/config/media_profiles.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml \
    device/google/cuttlefish/shared/permissions/cuttlefish_excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/cuttlefish_excluded_hardware.xml \
    device/google/cuttlefish/shared/permissions/privapp-permissions-cuttlefish.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/privapp-permissions-cuttlefish.xml \
    frameworks/av/media/libeffects/data/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_telephony.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_telephony.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_configuration_generic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/surround_sound_configuration_5_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/surround_sound_configuration_5_0.xml \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.concurrent.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.concurrent.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.raw.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.raw.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.faketouch.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.faketouch.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.reboot_escrow.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.reboot_escrow.xml \
    frameworks/native/data/etc/android.hardware.sensor.ambient_temperature.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.ambient_temperature.xml \
    frameworks/native/data/etc/android.hardware.sensor.barometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.barometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.hinge_angle.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.hinge_angle.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.sensor.relative_humidity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.relative_humidity.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.software.ipsec_tunnels.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.ipsec_tunnels.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.verified_boot.xml \
    system/bt/vendor_libs/test_vendor_lib/data/controller_properties.json:vendor/etc/bluetooth/controller_properties.json \
    device/google/cuttlefish/shared/config/task_profiles.json:$(TARGET_COPY_OUT_VENDOR)/etc/task_profiles.json \
    device/google/cuttlefish/shared/config/fstab.f2fs:$(TARGET_COPY_OUT_RAMDISK)/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab.f2fs:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab.f2fs:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab.ext4:$(TARGET_COPY_OUT_RAMDISK)/fstab.ext4 \
    device/google/cuttlefish/shared/config/fstab.ext4:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.ext4 \
    device/google/cuttlefish/shared/config/fstab.ext4:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/fstab.ext4

ifeq ($(TARGET_VULKAN_SUPPORT),true)
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.vulkan.level-0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_0_3.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.software.vulkan.deqp.level-2020-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.vulkan.deqp.level.xml
endif

# Packages for HAL implementations

#
# Atrace HAL
#
PRODUCT_PACKAGES += \
    android.hardware.atrace@1.0-service

#
# Authsecret HAL
#
PRODUCT_PACKAGES += \
    android.hardware.authsecret@1.0-service

#
# Hardware Composer HAL
#
PRODUCT_PACKAGES += \
    hwcomposer.drm_minigbm \
    hwcomposer.cutf_cvm_ashmem \
    hwcomposer.cutf_hwc2 \
    hwcomposer-stats \
    android.hardware.graphics.composer@2.2-impl \
    android.hardware.graphics.composer@2.2-service

#
# Gralloc HAL
#
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@4.0-service.minigbm \
    android.hardware.graphics.mapper@4.0-impl.minigbm

#
# Bluetooth HAL and Compatibility Bluetooth library (for older revs).
#
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.1-service.sim \
    android.hardware.bluetooth.audio@2.0-impl

#
# Audio HAL
#
PRODUCT_PACKAGES += \
    audio.primary.cutf \
    audio.r_submix.default \
    android.hardware.audio@6.0-impl:32 \
    android.hardware.audio.effect@6.0-impl:32 \
    android.hardware.audio@2.0-service \

#
# BiometricsFace HAL
#
PRODUCT_PACKAGES += \
    android.hardware.biometrics.face@1.0-service.example

#
# Contexthub HAL
#
PRODUCT_PACKAGES += \
    android.hardware.contexthub@1.1-service.mock

#
# Drm HAL
#
PRODUCT_PACKAGES += \
    android.hardware.drm@1.3-service.clearkey \
    android.hardware.drm@1.3-service.widevine

#
# Dumpstate HAL
#
ifeq ($(LOCAL_DUMPSTATE_PRODUCT_PACKAGE),)
    LOCAL_DUMPSTATE_PRODUCT_PACKAGE := android.hardware.dumpstate@1.1-service.example
endif
PRODUCT_PACKAGES += $(LOCAL_DUMPSTATE_PRODUCT_PACKAGE)

#
# Camera
#
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.6-service-google \
    libgooglecamerahwl_impl \
    android.hardware.camera.provider@2.6-impl-google \

#
# Gatekeeper
#
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper@1.0-service.software

#
# GPS
#
PRODUCT_PACKAGES += \
    android.hardware.gnss@2.1-service

# Health
ifeq ($(LOCAL_HEALTH_PRODUCT_PACKAGE),)
    LOCAL_HEALTH_PRODUCT_PACKAGE := \
    android.hardware.health@2.1-impl-cuttlefish \
    android.hardware.health@2.1-service
endif
PRODUCT_PACKAGES += $(LOCAL_HEALTH_PRODUCT_PACKAGE)

# Health Storage
PRODUCT_PACKAGES += \
    android.hardware.health.storage@1.0-service.cuttlefish

# Identity Credential
PRODUCT_PACKAGES += \
    android.hardware.identity-service.example

# Input Classifier HAL
PRODUCT_PACKAGES += \
    android.hardware.input.classifier@1.0-service.default

#
# Sensors
#
ifeq ($(LOCAL_SENSOR_PRODUCT_PACKAGE),)
       LOCAL_SENSOR_PRODUCT_PACKAGE := android.hardware.sensors@2.1-service.mock
endif
PRODUCT_PACKAGES += \
    $(LOCAL_SENSOR_PRODUCT_PACKAGE)
#
# Thermal (mock)
#
PRODUCT_PACKAGES += \
    android.hardware.thermal@2.0-service.mock

#
# Lights
#
PRODUCT_PACKAGES += \
    android.hardware.lights-service.example \

#
# Keymaster HAL
#
PRODUCT_PACKAGES += \
     android.hardware.keymaster@4.1-service

#
# Power HAL
#
PRODUCT_PACKAGES += \
    android.hardware.power-service.example

#
# PowerStats HAL
#
PRODUCT_PACKAGES += \
    android.hardware.power.stats@1.0-service.mock

#
# NeuralNetworks HAL
#
PRODUCT_PACKAGES += \
    android.hardware.neuralnetworks@1.3-service-sample-all \
    android.hardware.neuralnetworks@1.3-service-sample-float-fast \
    android.hardware.neuralnetworks@1.3-service-sample-float-slow \
    android.hardware.neuralnetworks@1.3-service-sample-minimal \
    android.hardware.neuralnetworks@1.3-service-sample-quant

#
# USB
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service

# Vibrator HAL
PRODUCT_PACKAGES += \
    android.hardware.vibrator-service.example

# BootControl HAL
PRODUCT_PACKAGES += \
    android.hardware.boot@1.1-impl \
    android.hardware.boot@1.1-impl.recovery \
    android.hardware.boot@1.1-service

# RebootEscrow HAL
PRODUCT_PACKAGES += \
    android.hardware.rebootescrow-service.default

# WLAN driver configuration files
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf

# Recovery mode
ifneq ($(TARGET_NO_RECOVERY),true)

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/init.recovery.rc:$(TARGET_COPY_OUT_RECOVERY)/root/init.recovery.cutf_cvm.rc \
    device/google/cuttlefish/shared/config/cgroups.json:$(TARGET_COPY_OUT_RECOVERY)/root/vendor/etc/cgroups.json \
    device/google/cuttlefish/shared/config/ueventd.rc:$(TARGET_COPY_OUT_RECOVERY)/root/ueventd.cutf_cvm.rc \

endif

#
# Shell script Vendor Module Loading
#
PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/config/init.insmod.sh:$(TARGET_COPY_OUT_VENDOR)/bin/init.insmod.sh \

# Host packages to install
PRODUCT_HOST_PACKAGES += socket_vsock_proxy

PRODUCT_EXTRA_VNDK_VERSIONS := 28 29

PRODUCT_SOONG_NAMESPACES += external/mesa3d

# Need this so that the application's loop on reading input can be synchronized
# with HW VSYNC
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += ro.surface_flinger.running_without_sync_framework=true
