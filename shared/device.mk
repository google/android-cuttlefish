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

# Enforce generic ramdisk allow list
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_ramdisk.mk)

# Set Vendor SPL to match platform
VENDOR_SECURITY_PATCH = $(PLATFORM_SECURITY_PATCH)

# Set boot SPL
BOOT_SECURITY_PATCH = $(PLATFORM_SECURITY_PATCH)

PRODUCT_SOONG_NAMESPACES += device/generic/goldfish-opengl # for vulkan
PRODUCT_SOONG_NAMESPACES += device/generic/goldfish # for audio and wifi

PRODUCT_SHIPPING_API_LEVEL := 33
PRODUCT_USE_DYNAMIC_PARTITIONS := true
DISABLE_RILD_OEM_HOOK := true

PRODUCT_SET_DEBUGFS_RESTRICTIONS := true

PRODUCT_SOONG_NAMESPACES += device/generic/goldfish-opengl # for vulkan

PRODUCT_FS_COMPRESSION := 1
TARGET_RO_FILE_SYSTEM_TYPE ?= ext4
TARGET_USERDATAIMAGE_FILE_SYSTEM_TYPE ?= f2fs
TARGET_USERDATAIMAGE_PARTITION_SIZE ?= 6442450944

TARGET_VULKAN_SUPPORT ?= true
TARGET_ENABLE_HOST_BLUETOOTH_EMULATION ?= true
TARGET_USE_BTLINUX_HAL_IMPL ?= true

# TODO(b/65201432): Swiftshader needs to create executable memory.
PRODUCT_REQUIRES_INSECURE_EXECMEM_FOR_SWIFTSHADER := true

AB_OTA_UPDATER := true
AB_OTA_PARTITIONS += \
    boot \
    odm \
    odm_dlkm \
    product \
    system \
    system_ext \
    vbmeta \
    vbmeta_system \
    vendor \
    vendor_boot \
    vendor_dlkm \

# Enable Virtual A/B
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/compression_with_xor.mk)

# Enable Scoped Storage related
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

# Properties that are not vendor-specific. These will go in the product
# partition, instead of the vendor partition, and do not need vendor
# sepolicy
PRODUCT_PRODUCT_PROPERTIES += \
    persist.adb.tcp.port=5555 \
    ro.com.google.locationfeatures=1 \
    persist.sys.fuse.passthrough.enable=true \

# Explanation of specific properties:
#   debug.hwui.swap_with_damage avoids boot failure on M http://b/25152138
#   ro.hardware.keystore_desede=true needed for CtsKeystoreTestCases
PRODUCT_VENDOR_PROPERTIES += \
    tombstoned.max_tombstone_count=500 \
    vendor.bt.rootcanal_test_console=off \
    debug.hwui.swap_with_damage=0 \
    ro.carrier=unknown \
    ro.com.android.dataroaming?=false \
    ro.hardware.virtual_device=1 \
    ro.logd.size=1M \
    wifi.interface=wlan0 \
    persist.sys.zram_enabled=1 \
    ro.hardware.keystore_desede=true \
    ro.rebootescrow.device=/dev/block/pmem0 \
    ro.incremental.enable=1 \
    debug.c2.use_dmabufheaps=1 \
    ro.camerax.extensions.enabled=true \

LOCAL_BT_PROPERTIES ?= \
 vendor.ser.bt-uart?=/dev/hvc5 \

PRODUCT_VENDOR_PROPERTIES += \
	 ${LOCAL_BT_PROPERTIES} \

# Below is a list of properties we probably should get rid of.
PRODUCT_VENDOR_PROPERTIES += \
    wlan.driver.status=ok

ifneq ($(LOCAL_DISABLE_OMX),true)
# Codec 1.0 requires the OMX services
DEVICE_MANIFEST_FILE += \
    device/google/cuttlefish/shared/config/android.hardware.media.omx@1.0.xml
endif

PRODUCT_VENDOR_PROPERTIES += \
    debug.stagefright.c2inputsurface=-1

# Enforce privapp permissions control.
PRODUCT_VENDOR_PROPERTIES += ro.control_privapp_permissions?=enforce

# aes-256-heh default is not supported in standard kernels.
PRODUCT_VENDOR_PROPERTIES += ro.crypto.volume.filenames_mode=aes-256-cts

# Copy preopted files from system_b on first boot
PRODUCT_VENDOR_PROPERTIES += ro.cp_system_other_odex=1

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/otapreopt_script \
    FILESYSTEM_TYPE_system=ext4 \
    POSTINSTALL_OPTIONAL_system=true

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_vendor=true \
    POSTINSTALL_PATH_vendor=bin/checkpoint_gc \
    FILESYSTEM_TYPE_vendor=ext4 \
    POSTINSTALL_OPTIONAL_vendor=true

# Userdata Checkpointing OTA GC
PRODUCT_PACKAGES += \
    checkpoint_gc

# Enable CameraX extension sample
PRODUCT_PACKAGES += androidx.camera.extensions.impl sample_camera_extensions.xml

# DRM service opt-in
PRODUCT_VENDOR_PROPERTIES += drm.service.enabled=true

# Call deleteAllKeys if vold detects a factory reset
PRODUCT_VENDOR_PROPERTIES += ro.crypto.metadata_init_delete_all_keys.enabled=true

PRODUCT_SOONG_NAMESPACES += hardware/google/camera
PRODUCT_SOONG_NAMESPACES += hardware/google/camera/devices/EmulatedCamera

#
# Packages for various GCE-specific utilities
#
PRODUCT_PACKAGES += \
    CuttlefishService \
    cuttlefish_sensor_injection \
    socket_vsock_proxy \
    tombstone_transmit \
    tombstone_producer \
    suspend_blocker \
    vsoc_input_service \
    vtpm_manager \

$(call soong_config_append, cvd, launch_configs, cvd_config_auto.json cvd_config_phone.json cvd_config_tablet.json cvd_config_tv.json)
$(call soong_config_append, cvd, grub_config, grub.cfg)

#
# Packages for AOSP-available stuff we use from the framework
#
PRODUCT_PACKAGES += \
    e2fsck \
    ip \
    sleep \
    tcpdump \
    wificond \

#
# Packages for the OpenGL implementation
#

# ANGLE provides an OpenGL implementation built on top of Vulkan.
PRODUCT_PACKAGES += \
    libEGL_angle \
    libGLESv1_CM_angle \
    libGLESv2_angle \
    libfeature_support_angle.so

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
    hwcomposer.ranchu \
    libandroidemu \
    libOpenglCodecCommon \
    libOpenglSystemCommon \
    libGLESv1_CM_emulation \
    lib_renderControl_enc \
    libEGL_emulation \
    libGLESv2_enc \
    libGLESv2_emulation \
    libGLESv1_enc \
    libGoldfishProfiler \

#
# Packages for testing
#
PRODUCT_PACKAGES += \
    aidl_lazy_test_server \
    aidl_lazy_cb_test_server \
    hidl_lazy_test_server \
    hidl_lazy_cb_test_server

# Runtime Resource Overlays
ifneq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_PACKAGES += \
    cuttlefish_overlay_frameworks_base_core \
    cuttlefish_overlay_settings_provider \

endif

# PRODUCT_AAPT_CONFIG and PRODUCT_AAPT_PREF_CONFIG are intentionally not set to
# pick up every density resources.

#
# Common manifest for all targets
#
DEVICE_MANIFEST_FILE += device/google/cuttlefish/shared/config/manifest.xml

#
# General files
#


ifneq ($(LOCAL_SENSOR_FILE_OVERRIDES),true)
ifneq ($(LOCAL_PREFER_VENDOR_APEX),true)
    PRODUCT_COPY_FILES += \
        frameworks/native/data/etc/android.hardware.sensor.ambient_temperature.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.ambient_temperature.xml \
        frameworks/native/data/etc/android.hardware.sensor.barometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.barometer.xml \
        frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
        frameworks/native/data/etc/android.hardware.sensor.hinge_angle.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.hinge_angle.xml \
        frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
        frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
        frameworks/native/data/etc/android.hardware.sensor.relative_humidity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.relative_humidity.xml
endif
endif

ifneq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/permissions/cuttlefish_excluded_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/cuttlefish_excluded_hardware.xml \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.camera.concurrent.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.concurrent.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.raw.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.raw.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.reboot_escrow.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.reboot_escrow.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \
    frameworks/native/data/etc/android.software.ipsec_tunnels.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.ipsec_tunnels.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.verified_boot.xml
endif
PRODUCT_COPY_FILES += \
    hardware/google/camera/devices/EmulatedCamera/hwl/configs/emu_camera_back.json:$(TARGET_COPY_OUT_VENDOR)/etc/config/emu_camera_back.json \
    hardware/google/camera/devices/EmulatedCamera/hwl/configs/emu_camera_front.json:$(TARGET_COPY_OUT_VENDOR)/etc/config/emu_camera_front.json \
    hardware/google/camera/devices/EmulatedCamera/hwl/configs/emu_camera_depth.json:$(TARGET_COPY_OUT_VENDOR)/etc/config/emu_camera_depth.json \
    device/google/cuttlefish/shared/config/init.vendor.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.cutf_cvm.rc \
    device/google/cuttlefish/shared/config/init.product.rc:$(TARGET_COPY_OUT_PRODUCT)/etc/init/init.rc \
    device/google/cuttlefish/shared/config/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc \
    device/google/cuttlefish/shared/config/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    device/google/cuttlefish/shared/config/media_codecs_google_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video.xml \
    device/google/cuttlefish/shared/config/media_codecs_performance.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_performance.xml \
    device/google/cuttlefish/shared/config/media_profiles.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml \
    device/google/cuttlefish/shared/permissions/privapp-permissions-cuttlefish.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/privapp-permissions-cuttlefish.xml \
    frameworks/av/media/libeffects/data/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_telephony.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_telephony.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/surround_sound_configuration_5_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/surround_sound_configuration_5_0.xml \
    device/google/cuttlefish/shared/config/task_profiles.json:$(TARGET_COPY_OUT_VENDOR)/etc/task_profiles.json \

# TODO(b/205065320): remove this when wifi vendor apex support mac80211_hwsim
ifeq ($(PRODUCT_ENFORCE_MAC80211_HWSIM),true)
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \

endif

ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_PACKAGES += com.google.cf.input.config
else
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/input/Crosvm_Virtio_Multitouch_Touchscreen_0.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Crosvm_Virtio_Multitouch_Touchscreen_0.idc \
    device/google/cuttlefish/shared/config/input/Crosvm_Virtio_Multitouch_Touchscreen_1.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Crosvm_Virtio_Multitouch_Touchscreen_1.idc \
    device/google/cuttlefish/shared/config/input/Crosvm_Virtio_Multitouch_Touchscreen_2.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Crosvm_Virtio_Multitouch_Touchscreen_2.idc \
    device/google/cuttlefish/shared/config/input/Crosvm_Virtio_Multitouch_Touchscreen_3.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Crosvm_Virtio_Multitouch_Touchscreen_3.idc
endif

ifeq ($(TARGET_RO_FILE_SYSTEM_TYPE),ext4)
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/fstab.f2fs:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab.f2fs:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab.f2fs:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab.ext4:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.ext4 \
    device/google/cuttlefish/shared/config/fstab.ext4:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.ext4 \
    device/google/cuttlefish/shared/config/fstab.ext4:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/fstab.ext4
else
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/fstab-$(TARGET_RO_FILE_SYSTEM_TYPE).f2fs:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab-$(TARGET_RO_FILE_SYSTEM_TYPE).f2fs:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab-$(TARGET_RO_FILE_SYSTEM_TYPE).f2fs:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/fstab.f2fs \
    device/google/cuttlefish/shared/config/fstab-$(TARGET_RO_FILE_SYSTEM_TYPE).ext4:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.ext4 \
    device/google/cuttlefish/shared/config/fstab-$(TARGET_RO_FILE_SYSTEM_TYPE).ext4:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.ext4 \
    device/google/cuttlefish/shared/config/fstab-$(TARGET_RO_FILE_SYSTEM_TYPE).ext4:$(TARGET_COPY_OUT_RECOVERY)/root/first_stage_ramdisk/fstab.ext4
endif

ifeq ($(TARGET_VULKAN_SUPPORT),true)
ifneq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.vulkan.level-0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_0_3.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.software.vulkan.deqp.level-2021-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.vulkan.deqp.level.xml \
    frameworks/native/data/etc/android.software.opengles.deqp.level-2021-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.opengles.deqp.level.xml
endif
endif

# Packages for HAL implementations

#
# Atrace HAL
#
PRODUCT_PACKAGES += \
    android.hardware.atrace@1.0-service

#
# Weaver aidl HAL
#
PRODUCT_PACKAGES += \
    android.hardware.weaver-service.example

#
# OemLock aidl HAL
#
PRODUCT_PACKAGES += \
    android.hardware.oemlock-service.example

#
# Authsecret HAL
#
PRODUCT_PACKAGES += \
    android.hardware.authsecret@1.0-service

#
# Authsecret AIDL HAL
#
PRODUCT_PACKAGES += \
    android.hardware.authsecret-service.example
#
# Hardware Composer HAL
#
PRODUCT_PACKAGES += \
    hwcomposer.drm_minigbm \
    hwcomposer.cutf \
    hwcomposer-stats \
    android.hardware.graphics.composer@2.4-service

#
# Gralloc HAL
#
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@4.0-service.minigbm \
    android.hardware.graphics.mapper@4.0-impl.minigbm

#
# Bluetooth HAL and Compatibility Bluetooth library (for older revs).
#
ifneq ($(LOCAL_PREFER_VENDOR_APEX),true)
ifeq ($(LOCAL_BLUETOOTH_PRODUCT_PACKAGE),)
ifeq ($(TARGET_ENABLE_HOST_BLUETOOTH_EMULATION),true)
ifeq ($(TARGET_USE_BTLINUX_HAL_IMPL),true)
    LOCAL_BLUETOOTH_PRODUCT_PACKAGE := android.hardware.bluetooth@1.1-service.btlinux
else
    LOCAL_BLUETOOTH_PRODUCT_PACKAGE := android.hardware.bluetooth@1.1-service.remote
endif
else
    LOCAL_BLUETOOTH_PRODUCT_PACKAGE := android.hardware.bluetooth@1.1-service.sim
endif
    DEVICE_MANIFEST_FILE += device/google/cuttlefish/shared/config/manifest_android.hardware.bluetooth@1.1-service.xml
endif

PRODUCT_COPY_FILES +=\
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

PRODUCT_PACKAGES += $(LOCAL_BLUETOOTH_PRODUCT_PACKAGE)

PRODUCT_PACKAGES += android.hardware.bluetooth.audio@2.1-impl  bt_vhci_forwarder

# Bluetooth initialization configuration is copied to the init folder here instead of being added
# as an init_rc attribute of the bt_vhci_forward binary.  The bt_vhci_forward binary is used by
# multiple targets with different initialization configurations.
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/guest/commands/bt_vhci_forwarder/bt_vhci_forwarder.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/bt_vhci_forwarder.rc

else
PRODUCT_PACKAGES += com.google.cf.bt android.hardware.bluetooth.audio@2.1-impl
endif

#
# Audio HAL
#
LOCAL_AUDIO_PRODUCT_PACKAGE ?= \
    android.hardware.audio.service \
    android.hardware.audio@7.0-impl.ranchu \
    android.hardware.audio.effect@7.0-impl \

LOCAL_AUDIO_PRODUCT_COPY_FILES ?= \
    device/generic/goldfish/audio/policy/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    device/generic/goldfish/audio/policy/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/media/libeffects/data/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \

LOCAL_AUDIO_DEVICE_PACKAGE_OVERLAYS ?=

PRODUCT_PACKAGES += $(LOCAL_AUDIO_PRODUCT_PACKAGE)
PRODUCT_COPY_FILES += $(LOCAL_AUDIO_PRODUCT_COPY_FILES)
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_AUDIO_DEVICE_PACKAGE_OVERLAYS)

#
# BiometricsFace HAL (HIDL)
#
PRODUCT_PACKAGES += \
    android.hardware.biometrics.face@1.0-service.example

#
# BiometricsFingerprint HAL (HIDL)
#
PRODUCT_PACKAGES += \
    android.hardware.biometrics.fingerprint@2.2-service.example

#
# BiometricsFace HAL (AIDL)
#
PRODUCT_PACKAGES += \
    android.hardware.biometrics.face-service.example

#
# BiometricsFingerprint HAL (AIDL)
#
PRODUCT_PACKAGES += \
    android.hardware.biometrics.fingerprint-service.example

#
# Contexthub HAL
#
PRODUCT_PACKAGES += \
    android.hardware.contexthub@1.2-service.mock

#
# Drm HAL
#
PRODUCT_PACKAGES += \
    android.hardware.drm@1.4-service.clearkey \
    android.hardware.drm@latest-service.widevine

#
# Confirmation UI HAL
#
ifeq ($(LOCAL_CONFIRMATIONUI_PRODUCT_PACKAGE),)
    LOCAL_CONFIRMATIONUI_PRODUCT_PACKAGE := android.hardware.confirmationui@1.0-service.cuttlefish
endif
PRODUCT_PACKAGES += $(LOCAL_CONFIRMATIONUI_PRODUCT_PACKAGE)

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
ifeq ($(TARGET_USE_VSOCK_CAMERA_HAL_IMPL),true)
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.7-external-vsock-service \
    android.hardware.camera.provider@2.7-impl-cuttlefish
DEVICE_MANIFEST_FILE += \
    device/google/cuttlefish/guest/hals/camera/manifest.xml
else
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.7-service-google \
    libgooglecamerahwl_impl \
    android.hardware.camera.provider@2.7-impl-google \

endif
#
# Gatekeeper
#
ifeq ($(LOCAL_GATEKEEPER_PRODUCT_PACKAGE),)
       LOCAL_GATEKEEPER_PRODUCT_PACKAGE := android.hardware.gatekeeper@1.0-service.remote
endif
PRODUCT_PACKAGES += \
    $(LOCAL_GATEKEEPER_PRODUCT_PACKAGE)

#
# GPS
#
PRODUCT_PACKAGES += \
    android.hardware.gnss-service.example

# Health
ifeq ($(LOCAL_HEALTH_PRODUCT_PACKAGE),)
    LOCAL_HEALTH_PRODUCT_PACKAGE := \
    android.hardware.health@2.1-impl-cuttlefish \
    android.hardware.health@2.1-service
endif
PRODUCT_PACKAGES += $(LOCAL_HEALTH_PRODUCT_PACKAGE)

# Health Storage
PRODUCT_PACKAGES += \
    android.hardware.health.storage-service.cuttlefish

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
ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
       LOCAL_SENSOR_PRODUCT_PACKAGE := com.android.hardware.sensors
else
       LOCAL_SENSOR_PRODUCT_PACKAGE := android.hardware.sensors@2.1-service.mock
endif
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
# KeyMint HAL
#
ifeq ($(LOCAL_KEYMINT_PRODUCT_PACKAGE),)
       LOCAL_KEYMINT_PRODUCT_PACKAGE := android.hardware.security.keymint-service.remote
# Indicate that this KeyMint includes support for the ATTEST_KEY key purpose.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.keystore.app_attest_key.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.keystore.app_attest_key.xml
endif
 PRODUCT_PACKAGES += \
    $(LOCAL_KEYMINT_PRODUCT_PACKAGE)

# Keymint configuration
ifneq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.software.device_id_attestation.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.device_id_attestation.xml
endif

#
# Power and PowerStats HALs
#
ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_PACKAGES += com.android.hardware.power
else
PRODUCT_PACKAGES += \
    android.hardware.power-service.example \
    android.hardware.power.stats-service.example \

endif

#
# NeuralNetworks HAL
#
PRODUCT_PACKAGES += \
    android.hardware.neuralnetworks@1.3-service-sample-all \
    android.hardware.neuralnetworks@1.3-service-sample-float-fast \
    android.hardware.neuralnetworks@1.3-service-sample-float-slow \
    android.hardware.neuralnetworks@1.3-service-sample-minimal \
    android.hardware.neuralnetworks@1.3-service-sample-quant \
    android.hardware.neuralnetworks-service-sample-all \
    android.hardware.neuralnetworks-service-sample-float-fast \
    android.hardware.neuralnetworks-service-sample-float-slow \
    android.hardware.neuralnetworks-service-sample-minimal \
    android.hardware.neuralnetworks-service-sample-quant \
    android.hardware.neuralnetworks-shim-service-sample

#
# USB
ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_PACKAGES += \
    com.android.hardware.usb
else
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service
endif

# Vibrator HAL
ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_PACKAGES += com.android.hardware.vibrator
else
PRODUCT_PACKAGES += \
    android.hardware.vibrator-service.example
endif

# BootControl HAL
PRODUCT_PACKAGES += \
    android.hardware.boot@1.2-impl \
    android.hardware.boot@1.2-impl.recovery \
    android.hardware.boot@1.2-service

# RebootEscrow HAL
PRODUCT_PACKAGES += \
    android.hardware.rebootescrow-service.default

# Memtrack HAL
PRODUCT_PACKAGES += \
    android.hardware.memtrack-service.example

# Fastboot HAL & fastbootd
PRODUCT_PACKAGES += \
    android.hardware.fastboot@1.1-impl-mock \
    fastbootd

# Recovery mode
ifneq ($(TARGET_NO_RECOVERY),true)

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/init.recovery.rc:$(TARGET_COPY_OUT_RECOVERY)/root/init.recovery.cutf_cvm.rc \
    device/google/cuttlefish/shared/config/cgroups.json:$(TARGET_COPY_OUT_RECOVERY)/root/vendor/etc/cgroups.json \
    device/google/cuttlefish/shared/config/ueventd.rc:$(TARGET_COPY_OUT_RECOVERY)/root/ueventd.cutf_cvm.rc \

PRODUCT_PACKAGES += \
    update_engine_sideload

endif

ifdef TARGET_DEDICATED_RECOVERY
PRODUCT_BUILD_RECOVERY_IMAGE := true
PRODUCT_PACKAGES += linker.vendor_ramdisk shell_and_utilities_vendor_ramdisk
else
PRODUCT_PACKAGES += linker.recovery shell_and_utilities_recovery
endif

# wifi

# TODO(b/205065320): remove this when wifi vendor apex support mac80211_hwsim
LOCAL_USE_WIFI_VENDOR_APEX := false
ifneq ($(PRODUCT_ENFORCE_MAC80211_HWSIM),true)
ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
LOCAL_USE_WIFI_VENDOR_APEX := true
endif
endif

ifeq ($(LOCAL_USE_WIFI_VENDOR_APEX),true)

PRODUCT_PACKAGES += com.google.cf.wifi
# Demonstrate multi-installed vendor APEXes by installing another wifi HAL vendor APEX
# which does not include the passpoint feature XML.
#
# The default is set in BoardConfig.mk using bootconfig.
# This can be changed at CVD launch-time using
#     --extra_bootconfig_args "androidboot.vendor.apex.com.android.wifi.hal:=X"
# or post-launch, at runtime using
#     setprop persist.vendor.apex.com.android.wifi.hal X && reboot
# where X is the name of the APEX file to use.
PRODUCT_PACKAGES += com.google.cf.wifi.no-passpoint

$(call add_soong_config_namespace, wpa_supplicant)
$(call add_soong_config_var_value, wpa_supplicant, platform_version, $(PLATFORM_VERSION))
$(call add_soong_config_var_value, wpa_supplicant, nl80211_driver, CONFIG_DRIVER_NL80211_QCA)
# TODO(b/205065320): Convert com.google.cf.wifi to use mac8011_hwsim_virtio
PRODUCT_VENDOR_PROPERTIES += ro.vendor.wifi_impl=virt_wifi
else

PRODUCT_PACKAGES += \
    rename_netiface \
    wpa_supplicant
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/wpa_supplicant.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/wpa_supplicant.rc

# WLAN driver configuration files
LOCAL_WPA_SUPPLICANT_OVERLAY ?= $(LOCAL_PATH)/config/wpa_supplicant_overlay.conf
PRODUCT_COPY_FILES += \
    external/wpa_supplicant_8/wpa_supplicant/wpa_supplicant_template.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    $(LOCAL_WPA_SUPPLICANT_OVERLAY):$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf

ifeq ($(PRODUCT_ENFORCE_MAC80211_HWSIM),true)
PRODUCT_PACKAGES += \
    mac80211_create_radios \
    hostapd \
    android.hardware.wifi@1.0-service

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/guest/services/wifi/init.wifi.sh:$(TARGET_COPY_OUT_VENDOR)/bin/init.wifi.sh \

PRODUCT_VENDOR_PROPERTIES += ro.vendor.wifi_impl=mac8011_hwsim_virtio

$(call soong_config_append,cvdhost,enforce_mac80211_hwsim,true)

else
PRODUCT_PACKAGES += setup_wifi
PRODUCT_VENDOR_PROPERTIES += ro.vendor.wifi_impl=virt_wifi
endif

endif

# Host packages to install
PRODUCT_HOST_PACKAGES += socket_vsock_proxy

PRODUCT_EXTRA_VNDK_VERSIONS := 28 29 30 31

PRODUCT_SOONG_NAMESPACES += external/mesa3d

#for Confirmation UI
PRODUCT_SOONG_NAMESPACES += vendor/google_devices/common/proprietary/confirmatioui_hal

# Need this so that the application's loop on reading input can be synchronized
# with HW VSYNC
PRODUCT_VENDOR_PROPERTIES += \
    ro.surface_flinger.running_without_sync_framework=true

# Enable GPU-intensive background blur support on Cuttlefish when requested by apps
PRODUCT_VENDOR_PROPERTIES += \
    ro.surface_flinger.supports_background_blur 1

# Set support one-handed mode
PRODUCT_PRODUCT_PROPERTIES += \
    ro.support_one_handed_mode=true

# Set one_handed_mode screen translate offset percentage
PRODUCT_PRODUCT_PROPERTIES += \
    persist.debug.one_handed_offset_percentage=50

# Set one_handed_mode translate animation duration milliseconds
PRODUCT_PRODUCT_PROPERTIES += \
    persist.debug.one_handed_translate_animation_duration=300

# Vendor Dlkm Locader
PRODUCT_PACKAGES += \
   dlkm_loader
