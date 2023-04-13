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

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.boot_security_patch=$(BOOT_SECURITY_PATCH)

PRODUCT_SOONG_NAMESPACES += device/generic/goldfish # for audio and wifi

PRODUCT_SHIPPING_API_LEVEL := 34
PRODUCT_USE_DYNAMIC_PARTITIONS := true
DISABLE_RILD_OEM_HOOK := true

# TODO(b/205788876) remove this condition when openwrt has an image for arm.
ifndef PRODUCT_ENFORCE_MAC80211_HWSIM
PRODUCT_ENFORCE_MAC80211_HWSIM := true
endif

PRODUCT_SET_DEBUGFS_RESTRICTIONS := true

PRODUCT_FS_COMPRESSION := 1
TARGET_RO_FILE_SYSTEM_TYPE ?= erofs
TARGET_USERDATAIMAGE_FILE_SYSTEM_TYPE ?= f2fs
TARGET_USERDATAIMAGE_PARTITION_SIZE ?= 6442450944

TARGET_VULKAN_SUPPORT ?= true

# Enable Virtual A/B
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/android_t_baseline.mk)
PRODUCT_VIRTUAL_AB_COMPRESSION_METHOD := gz

PRODUCT_VENDOR_PROPERTIES += ro.virtual_ab.compression.threads=true
PRODUCT_VENDOR_PROPERTIES += ro.virtual_ab.batch_writes=true

# Enable Scoped Storage related
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

# Properties that are not vendor-specific. These will go in the product
# partition, instead of the vendor partition, and do not need vendor
# sepolicy
PRODUCT_PRODUCT_PROPERTIES += \
    remote_provisioning.enable_rkpd=true \
    remote_provisioning.hostname=staging-remoteprovisioning.sandbox.googleapis.com \
    persist.adb.tcp.port=5555 \
    ro.com.google.locationfeatures=1 \
    persist.sys.fuse.passthrough.enable=true \
    remote_provisioning.tee.rkp_only=1

# Until we support adb keys on user builds, and fix logcat over serial,
# spawn adbd by default without authorization for "adb logcat"
ifeq ($(TARGET_BUILD_VARIANT),user)
PRODUCT_PRODUCT_PROPERTIES += \
    ro.adb.secure=0 \
    ro.debuggable=1
endif

# Explanation of specific properties:
#   ro.hardware.keystore_desede=true needed for CtsKeystoreTestCases
PRODUCT_VENDOR_PROPERTIES += \
    tombstoned.max_tombstone_count=500 \
    vendor.bt.rootcanal_test_console=off \
    ro.carrier=unknown \
    ro.com.android.dataroaming?=false \
    ro.hardware.virtual_device=1 \
    ro.logd.size=1M \
    wifi.interface=wlan0 \
    wifi.direct.interface=p2p-dev-wlan0 \
    persist.sys.zram_enabled=1 \
    ro.hardware.keystore_desede=true \
    ro.rebootescrow.device=/dev/block/pmem0 \
    ro.incremental.enable=1 \
    debug.c2.use_dmabufheaps=1

LOCAL_BT_PROPERTIES ?= \
 vendor.ser.bt-uart?=/dev/hvc5 \

PRODUCT_VENDOR_PROPERTIES += \
	 ${LOCAL_BT_PROPERTIES} \

# Below is a list of properties we probably should get rid of.
PRODUCT_VENDOR_PROPERTIES += \
    wlan.driver.status=ok

PRODUCT_VENDOR_PROPERTIES += \
    debug.stagefright.c2inputsurface=-1

# Enforce privapp permissions control.
PRODUCT_VENDOR_PROPERTIES += ro.control_privapp_permissions?=enforce

# Copy preopted files from system_b on first boot
PRODUCT_VENDOR_PROPERTIES += ro.cp_system_other_odex=1

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/otapreopt_script \
    FILESYSTEM_TYPE_system=erofs \
    POSTINSTALL_OPTIONAL_system=true

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_vendor=true \
    POSTINSTALL_PATH_vendor=bin/checkpoint_gc \
    FILESYSTEM_TYPE_vendor=erofs \
    POSTINSTALL_OPTIONAL_vendor=true

# Userdata Checkpointing OTA GC
PRODUCT_PACKAGES += \
    checkpoint_gc

# DRM service opt-in
PRODUCT_VENDOR_PROPERTIES += drm.service.enabled=true

# Call deleteAllKeys if vold detects a factory reset
PRODUCT_VENDOR_PROPERTIES += ro.crypto.metadata_init_delete_all_keys.enabled=true

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
    metrics_helper \

$(call soong_config_append,cvd,launch_configs,cvd_config_auto.json cvd_config_auto_md.json cvd_config_foldable.json cvd_config_go.json cvd_config_phone.json cvd_config_slim.json cvd_config_tablet.json cvd_config_tv.json cvd_config_wear.json)
$(call soong_config_append,cvd,grub_config,grub.cfg)

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
# Package for AOSP QNS
#
PRODUCT_PACKAGES += \
    QualifiedNetworksService

#
# Package for AOSP GBA
#
PRODUCT_PACKAGES += \
    GbaService

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
    cuttlefish_overlay_connectivity \
    cuttlefish_overlay_frameworks_base_core \
    cuttlefish_overlay_settings_provider

endif

#
# Satellite vendor service for CF
#
PRODUCT_PACKAGES += CFSatelliteService

# PRODUCT_AAPT_CONFIG and PRODUCT_AAPT_PREF_CONFIG are intentionally not set to
# pick up every density resources.

#
# Common manifest for all targets
#
LOCAL_DEVICE_FCM_MANIFEST_FILE ?= device/google/cuttlefish/shared/config/manifest.xml
DEVICE_MANIFEST_FILE += $(LOCAL_DEVICE_FCM_MANIFEST_FILE)

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
    frameworks/native/data/etc/android.hardware.ethernet.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.reboot_escrow.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.reboot_escrow.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \
    frameworks/native/data/etc/android.software.ipsec_tunnels.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.ipsec_tunnels.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.verified_boot.xml

endif
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.consumerir.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.consumerir.xml \
    device/google/cuttlefish/shared/config/init.vendor.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.cutf_cvm.rc \
    device/google/cuttlefish/shared/config/init.product.rc:$(TARGET_COPY_OUT_PRODUCT)/etc/init/init.rc \
    device/google/cuttlefish/shared/config/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc \
    device/google/cuttlefish/shared/config/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    device/google/cuttlefish/shared/config/media_codecs_google_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video.xml \
    device/google/cuttlefish/shared/config/media_codecs_performance.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_performance.xml \
    device/google/cuttlefish/shared/config/media_profiles.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml \
    device/google/cuttlefish/shared/permissions/privapp-permissions-cuttlefish.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/privapp-permissions-cuttlefish.xml \
    frameworks/av/media/libeffects/data/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    hardware/interfaces/audio/aidl/default/audio_effects_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects_config.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_telephony.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_telephony.xml \
    frameworks/av/services/audiopolicy/config/bluetooth_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration_7_0.xml \
    frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/surround_sound_configuration_5_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/surround_sound_configuration_5_0.xml \
    device/google/cuttlefish/shared/config/task_profiles.json:$(TARGET_COPY_OUT_VENDOR)/etc/task_profiles.json \
    frameworks/native/data/etc/android.software.credentials.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.credentials.xml \

ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_PACKAGES += com.google.cf.input.config
else
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/input/Crosvm_Virtio_Multitouch_Touchscreen_0.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Crosvm_Virtio_Multitouch_Touchscreen_0.idc \
    device/google/cuttlefish/shared/config/input/Crosvm_Virtio_Multitouch_Touchscreen_1.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Crosvm_Virtio_Multitouch_Touchscreen_1.idc \
    device/google/cuttlefish/shared/config/input/Crosvm_Virtio_Multitouch_Touchscreen_2.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Crosvm_Virtio_Multitouch_Touchscreen_2.idc \
    device/google/cuttlefish/shared/config/input/Crosvm_Virtio_Multitouch_Touchscreen_3.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/Crosvm_Virtio_Multitouch_Touchscreen_3.idc
endif

PRODUCT_PACKAGES += \
    fstab.cf.f2fs.hctr2 \
    fstab.cf.f2fs.hctr2.vendor_ramdisk \
    fstab.cf.f2fs.cts \
    fstab.cf.f2fs.cts.vendor_ramdisk \
    fstab.cf.ext4.hctr2 \
    fstab.cf.ext4.hctr2.vendor_ramdisk \
    fstab.cf.ext4.cts \
    fstab.cf.ext4.cts.vendor_ramdisk \

# Packages for HAL implementations

#
# Weaver aidl HAL
#
# TODO(b/262418065) Add a real weaver implementation

#
# IR aidl HAL
#
PRODUCT_PACKAGES += \
	android.hardware.ir-service.example \
	consumerir.default


#
# OemLock aidl HAL
#
PRODUCT_PACKAGES += \
    android.hardware.oemlock-service.example

#
# Authsecret AIDL HAL
#
PRODUCT_PACKAGES += \
    android.hardware.authsecret-service.example

#
# Bluetooth HAL and Compatibility Bluetooth library (for older revs).
#
ifneq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_COPY_FILES +=\
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

PRODUCT_PACKAGES += \
    android.hardware.bluetooth-service.default \
    bt_vhci_forwarder

# Bluetooth initialization configuration is copied to the init folder here instead of being added
# as an init_rc attribute of the bt_vhci_forward binary.  The bt_vhci_forward binary is used by
# multiple targets with different initialization configurations.
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/guest/commands/bt_vhci_forwarder/bt_vhci_forwarder.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/bt_vhci_forwarder.rc

else
PRODUCT_PACKAGES += com.google.cf.bt
endif

#
# Bluetooth Audio AIDL HAL
#
PRODUCT_PACKAGES += \
    android.hardware.bluetooth.audio-impl \

#
# Audio HAL
# Note: aidl services are loaded, however they are not fully functional yet,
#       and are not used by the framework, only by VTS tests.
#
ifndef LOCAL_AUDIO_PRODUCT_PACKAGE
LOCAL_AUDIO_PRODUCT_PACKAGE := \
    android.hardware.audio.service \
    android.hardware.audio@7.1-impl.ranchu \
    android.hardware.audio.effect@7.0-impl \
    android.hardware.audio.service-aidl.example \
    android.hardware.audio.effect.service-aidl.example \
    libaecsw \
    libagc1sw \
    libagc2sw \
    libbassboostsw \
    libbundleaidl \
    libdownmixaidl \
    libdynamicsprocessingaidl \
    libenvreverbsw \
    libequalizersw \
    libextensioneffect \
    libhapticgeneratoraidl \
    libloudnessenhanceraidl \
    libnssw \
    libpreprocessingaidl \
    libpresetreverbsw \
    libreverbaidl \
    libtinyxml2 \
    libvirtualizersw \
    libvisualizeraidl \
    libvolumesw
DEVICE_MANIFEST_FILE += \
    device/google/cuttlefish/guest/hals/audio/effects/manifest.xml
endif

ifndef LOCAL_AUDIO_PRODUCT_COPY_FILES
LOCAL_AUDIO_PRODUCT_COPY_FILES := \
    device/generic/goldfish/audio/policy/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    device/generic/goldfish/audio/policy/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/media/libeffects/data/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    hardware/interfaces/audio/aidl/default/audio_effects_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects_config.xml
endif

PRODUCT_PACKAGES += $(LOCAL_AUDIO_PRODUCT_PACKAGE)
PRODUCT_COPY_FILES += $(LOCAL_AUDIO_PRODUCT_COPY_FILES)
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_AUDIO_DEVICE_PACKAGE_OVERLAYS)

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
    android.hardware.contexthub-service.example

#
# Drm HAL
#
PRODUCT_PACKAGES += \
    android.hardware.drm@latest-service.clearkey \
    android.hardware.drm@latest-service.widevine

#
# Confirmation UI HAL
#
ifeq ($(LOCAL_CONFIRMATIONUI_PRODUCT_PACKAGE),)
    LOCAL_CONFIRMATIONUI_PRODUCT_PACKAGE := android.hardware.confirmationui-service.cuttlefish
endif
PRODUCT_PACKAGES += $(LOCAL_CONFIRMATIONUI_PRODUCT_PACKAGE)

#
# Dumpstate HAL
#
ifeq ($(LOCAL_DUMPSTATE_PRODUCT_PACKAGE),)
    LOCAL_DUMPSTATE_PRODUCT_PACKAGE += android.hardware.dumpstate-service.example
endif
PRODUCT_PACKAGES += $(LOCAL_DUMPSTATE_PRODUCT_PACKAGE)

#
# Gatekeeper
#
ifeq ($(LOCAL_GATEKEEPER_PRODUCT_PACKAGE),)
       LOCAL_GATEKEEPER_PRODUCT_PACKAGE := android.hardware.gatekeeper-service.remote
endif
PRODUCT_PACKAGES += \
    $(LOCAL_GATEKEEPER_PRODUCT_PACKAGE)

#
# GPS
#
LOCAL_GNSS_PRODUCT_PACKAGE ?= \
    android.hardware.gnss-service.example

PRODUCT_PACKAGES += $(LOCAL_GNSS_PRODUCT_PACKAGE)

# Health
ifeq ($(LOCAL_HEALTH_PRODUCT_PACKAGE),)
    LOCAL_HEALTH_PRODUCT_PACKAGE := \
    android.hardware.health-service.cuttlefish \
    android.hardware.health-service.cuttlefish_recovery \

endif
PRODUCT_PACKAGES += $(LOCAL_HEALTH_PRODUCT_PACKAGE)

# Health Storage
PRODUCT_PACKAGES += \
    android.hardware.health.storage-service.cuttlefish

# Identity Credential
PRODUCT_PACKAGES += \
    android.hardware.identity-service.remote

PRODUCT_PACKAGES += \
    android.hardware.input.processor-service.example

# Netlink Interceptor HAL
PRODUCT_PACKAGES += \
    android.hardware.net.nlinterceptor-service.default

#
# Sensors
#
ifeq ($(LOCAL_SENSOR_PRODUCT_PACKAGE),)
ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
       LOCAL_SENSOR_PRODUCT_PACKAGE := com.android.hardware.sensors
else
       LOCAL_SENSOR_PRODUCT_PACKAGE := android.hardware.sensors-service.example
endif
endif
PRODUCT_PACKAGES += \
    $(LOCAL_SENSOR_PRODUCT_PACKAGE)

#
# Lights
#
PRODUCT_PACKAGES += \
    android.hardware.lights-service.example \

#
# KeyMint HAL
#
ifeq ($(LOCAL_KEYMINT_PRODUCT_PACKAGE),)
    LOCAL_KEYMINT_PRODUCT_PACKAGE := android.hardware.security.keymint-service.rust
endif

ifeq ($(LOCAL_KEYMINT_PRODUCT_PACKAGE),android.hardware.security.keymint-service.rust)
    # KeyMint HAL has been overridden to force use of the Rust reference implementation.
    # Set the build config for secure_env to match.
    $(call soong_config_set,secure_env,keymint_impl,rust)
endif

PRODUCT_PACKAGES += \
    $(LOCAL_KEYMINT_PRODUCT_PACKAGE) \

# Indicate that KeyMint includes support for the ATTEST_KEY key purpose.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.keystore.app_attest_key.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.keystore.app_attest_key.xml

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
# Tetheroffload HAL
#
PRODUCT_PACKAGES += \
    android.hardware.tetheroffload-service.example

#
# Thermal HAL
#
LOCAL_THERMAL_HAL_PRODUCT_PACKAGE ?= android.hardware.thermal-service.example
PRODUCT_PACKAGES += $(LOCAL_THERMAL_HAL_PRODUCT_PACKAGE)

#
# NeuralNetworks HAL
#
PRODUCT_PACKAGES += \
    android.hardware.neuralnetworks-service-sample-all \
    android.hardware.neuralnetworks-service-sample-limited \
    android.hardware.neuralnetworks-shim-service-sample

# USB
PRODUCT_PACKAGES += \
    com.android.hardware.usb

# USB Gadget
PRODUCT_PACKAGES += \
    android.hardware.usb.gadget-service.example

# Vibrator HAL
ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
PRODUCT_PACKAGES += com.android.hardware.vibrator
else
PRODUCT_PACKAGES += \
    android.hardware.vibrator-service.example
endif

# BootControl HAL
PRODUCT_PACKAGES += \
    android.hardware.boot-service.default \
    android.hardware.boot-service.default_recovery


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
ifeq ($(LOCAL_PREFER_VENDOR_APEX),true)
# Add com.android.hardware.wifi for android.hardware.wifi-service
PRODUCT_PACKAGES += com.android.hardware.wifi
# Add com.google.cf.wifi for hostapd, wpa_supplicant, etc.
PRODUCT_PACKAGES += com.google.cf.wifi
$(call add_soong_config_namespace, wpa_supplicant)
$(call add_soong_config_var_value, wpa_supplicant, platform_version, $(PLATFORM_VERSION))
$(call add_soong_config_var_value, wpa_supplicant, nl80211_driver, CONFIG_DRIVER_NL80211_QCA)

else
PRODUCT_PACKAGES += \
    rename_netiface \
    wpa_supplicant \
    setup_wifi \
    mac80211_create_radios \
    hostapd \
    android.hardware.wifi-service \
    init.wifi
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/wpa_supplicant.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/wpa_supplicant.rc

# VirtWifi interface configuration
ifeq ($(DEVICE_VIRTWIFI_PORT),)
    DEVICE_VIRTWIFI_PORT := eth2
endif
PRODUCT_VENDOR_PROPERTIES += ro.vendor.virtwifi.port=${DEVICE_VIRTWIFI_PORT}

# WLAN driver configuration files
ifndef LOCAL_WPA_SUPPLICANT_OVERLAY
LOCAL_WPA_SUPPLICANT_OVERLAY := $(LOCAL_PATH)/config/wpa_supplicant_overlay.conf
endif

ifndef LOCAL_P2P_SUPPLICANT
LOCAL_P2P_SUPPLICANT := $(LOCAL_PATH)/config/p2p_supplicant.conf
endif

PRODUCT_COPY_FILES += \
    external/wpa_supplicant_8/wpa_supplicant/wpa_supplicant_template.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    $(LOCAL_WPA_SUPPLICANT_OVERLAY):$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf \
    $(LOCAL_P2P_SUPPLICANT):$(TARGET_COPY_OUT_VENDOR)/etc/wifi/p2p_supplicant.conf
endif

# Wifi Runtime Resource Overlay
PRODUCT_PACKAGES += \
    CuttlefishTetheringOverlay \
    CuttlefishWifiOverlay

ifeq ($(PRODUCT_ENFORCE_MAC80211_HWSIM),true)
PRODUCT_VENDOR_PROPERTIES += ro.vendor.wifi_impl=mac8011_hwsim_virtio
$(call soong_config_append,cvdhost,enforce_mac80211_hwsim,true)
else
PRODUCT_VENDOR_PROPERTIES += ro.vendor.wifi_impl=virt_wifi
endif

# UWB HAL
PRODUCT_PACKAGES += \
    android.hardware.uwb-service

# Host packages to install
PRODUCT_HOST_PACKAGES += socket_vsock_proxy

#for Confirmation UI
PRODUCT_SOONG_NAMESPACES += vendor/google_devices/common/proprietary/confirmatioui_hal

# Need this so that the application's loop on reading input can be synchronized
# with HW VSYNC
PRODUCT_VENDOR_PROPERTIES += \
    ro.surface_flinger.running_without_sync_framework=true

# Enable GPU-intensive background blur support on Cuttlefish when requested by apps
PRODUCT_VENDOR_PROPERTIES += \
    ro.surface_flinger.supports_background_blur=1

# Disable GPU-intensive background blur for widget picker
PRODUCT_SYSTEM_PROPERTIES += \
    ro.launcher.depth.widget=0

# Start fingerprint virtual HAL process
PRODUCT_VENDOR_PROPERTIES += ro.vendor.fingerprint_virtual_hal_start=true

# Vendor Dlkm Locader
PRODUCT_PACKAGES += \
   dlkm_loader

# NFC AIDL HAL
PRODUCT_PACKAGES += \
    android.hardware.nfc-service.cuttlefish

# CAS AIDL HAL
PRODUCT_PACKAGES += \
    android.hardware.cas-service.example

PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/pci.ids:$(TARGET_COPY_OUT_VENDOR)/pci.ids
