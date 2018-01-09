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

PRODUCT_COPY_FILES += device/google/cuttlefish_kernel/4.4-x86_64/kernel:kernel

# Explanation of specific properties:
#   debug.hwui.swap_with_damage avoids boot failure on M http://b/25152138
#   ro.opengles.version OpenGLES 2.0
PRODUCT_PROPERTY_OVERRIDES += \
    debug.hwui.swap_with_damage=0 \
    ro.adb.qemud=0 \
    ro.carrier=unknown \
    ro.com.android.dataroaming=false \
    ro.com.google.locationfeatures=1 \
    ro.debuggable=1 \
    ro.hardware.virtual_device=1 \
    ro.logd.size=1M \
    ro.opengles.version=131072 \
    ro.ril.gprsclass=10 \
    ro.ril.hsxpa=1 \
    ro.setupwizard.mode=DISABLED \
    wifi.interface=wlan0 \

# Below is a list of properties we probably should get rid of.
PRODUCT_PROPERTY_OVERRIDES += \
    wlan.driver.status=ok


# Default OMX service to non-Treble
PRODUCT_PROPERTY_OVERRIDES += \
    persist.media.treble_omx=false

#
# Packages for various cuttlefish-specific tests
#
PRODUCT_PACKAGES += \
    vsoc_guest_region_e2e_test \
    vsoc_driver_test

#
# Packages for various GCE-specific utilities
#
PRODUCT_PACKAGES += \
    audiotop \
    dhcpcd_wlan0 \
    gce_fs_monitor \
    socket_forward_proxy \
    usbforward \
    VSoCService \
    wifirouter \
    wificlient \
    wpa_supplicant.vsoc.conf \
    vsoc_input_service \
    record_audio \

#
# Packages for AOSP-available stuff we use from the framework
#
PRODUCT_PACKAGES += \
    dhcpcd-6.8.2 \
    dhcpcd-6.8.2.conf \
    e2fsck \
    ip \
    network \
    perf \
    scp \
    sleep \
    tcpdump \
    wpa_supplicant \
    wificond \

#
# Packages for the OpenGL implementation
# TODO(ghartman): Remove this vendor dependency when possible
#
PRODUCT_PACKAGES += \
    libEGL_swiftshader \
    libGLESv1_CM_swiftshader \
    libGLESv2_swiftshader \

DEVICE_PACKAGE_OVERLAYS := device/google/cuttlefish/shared/overlay
PRODUCT_AAPT_CONFIG := normal large xlarge hdpi xhdpi
PRODUCT_AAPT_PREF_CONFIG := xhdpi

#
# General files
#
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    device/google/cuttlefish/shared/config/camera_v1.json:vendor/etc/config/camera.json \
    device/google/cuttlefish/shared/config/init.vsoc.rc:root/init.vsoc.rc \
    device/google/cuttlefish/shared/config/media_codecs.xml:system/etc/media_codecs.xml \
    device/google/cuttlefish/shared/config/media_codecs_performance.xml:system/etc/media_codecs_performance.xml \
    device/google/cuttlefish/shared/config/media_profiles.xml:system/etc/media_profiles.xml \
    device/google/cuttlefish/shared/config/profile.root:root/profile \
    device/google/cuttlefish/shared/config/fstab.vsoc:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.vsoc \
    frameworks/av/media/libeffects/data/audio_effects.conf:system/etc/audio_effects.conf \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:system/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_telephony.xml:system/etc/media_codecs_google_telephony.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video.xml:system/etc/media_codecs_google_video.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:system/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:system/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:system/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.raw.xml:system/etc/permissions/android.hardware.camera.raw.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:system/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.barometer.xml:system/etc/permissions/android.hardware.sensor.barometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:system/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.xml:system/etc/permissions/android.hardware.touchscreen.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.software.app_widgets.xml:system/etc/permissions/android.software.app_widgets.xml \
    system/bt/vendor_libs/test_vendor_lib/data/controller_properties.json:system/etc/bluetooth/controller_properties.json \


#
# USB Specific
#
PRODUCT_COPY_FILES += \
    device/google/cuttlefish/shared/config/init.hardware.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.vsoc.usb.rc

# Packages for HAL implementations

#
# Hardware Composer HAL
#
PRODUCT_PACKAGES += \
    hwcomposer.vsoc \
    hwcomposer-stats \
    android.hardware.graphics.composer@2.1-impl \
    android.hardware.graphics.composer@2.1-service

#
# Gralloc HAL
#
PRODUCT_PACKAGES += \
    gralloc.vsoc \
    android.hardware.graphics.mapper@2.0-impl \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service

#
# Bluetooth HAL and Compatibility Bluetooth library (for older revs).
#
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-service.sim \
    libbt-vendor-build-test

#
# Audio HAL
#
PRODUCT_PACKAGES += \
    audio.primary.vsoc \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    android.hardware.audio@2.0-service

#
# Drm HAL
#
PRODUCT_PACKAGES += \
    android.hardware.drm@1.0-impl \
    android.hardware.drm@1.0-service

#
# Dumpstate HAL
#
PRODUCT_PACKAGES += \
    android.hardware.dumpstate@1.0-service.cuttlefish

#
# Camera
#
PRODUCT_PACKAGES += \
    camera.vsoc \
    camera.vsoc.jpeg \
    camera.device@3.2-impl \
    android.hardware.camera.provider@2.4-impl \
    android.hardware.camera.provider@2.4-service

#
# GPS
#
PRODUCT_PACKAGES += \
    gps.vsoc \
    android.hardware.gnss@1.0-impl \
    android.hardware.gnss@1.0-service

#
# Sensors
#
PRODUCT_PACKAGES += \
    sensors.vsoc \
    android.hardware.sensors@1.0-impl \
    android.hardware.sensors@1.0-service

#
# Lights
#
PRODUCT_PACKAGES += \
    lights.vsoc \
    android.hardware.light@2.0-impl \
    android.hardware.light@2.0-service

#
# Keymaster HAL
#
PRODUCT_PACKAGES += \
     android.hardware.keymaster@3.0-impl \
     android.hardware.keymaster@3.0-service

#
# Power HAL
#
PRODUCT_PACKAGES += \
    power.vsoc \
    android.hardware.power@1.0-impl \
    android.hardware.power@1.0-service

#
# USB
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service

# TODO vibrator HAL
# TODO thermal

PRODUCT_PACKAGES += \
    vsoc_mem_json \
    cuttlefish_dtb
