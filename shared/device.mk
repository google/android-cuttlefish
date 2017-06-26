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

ifeq (,$(CUTTLEFISH_KERNEL))
CUTTLEFISH_KERNEL := device/google/cuttlefish_kernel/3.18-x86_64/kernel
endif

PRODUCT_COPY_FILES += $(CUTTLEFISH_KERNEL):kernel

# Explanation of specific properties:
#   debug.hwui.swap_with_damage avoids boot failure on M http://b/25152138
#   ro.opengles.version OpenGLES 2.0

PRODUCT_PROPERTY_OVERRIDES += \
    debug.hwui.swap_with_damage=0 \
    ro.adb.qemud=0 \
    ro.carrier=unknown \
    ro.com.android.dataroaming=false \
    ro.debuggable=1 \
    ro.logd.size=1M \
    ro.opengles.version=131072 \
    ro.ril.gprsclass=10 \
    ro.ril.hsxpa=1

# Default OMX service to non-Treble
PRODUCT_PROPERTY_OVERRIDES += \
    persist.media.treble_omx=false

#
# Packages for various GCE-specific utilities
#
PRODUCT_PACKAGES += \
    audiotop \
    dhcpcd_wlan0 \
    gce_fs_monitor \
    gce_init \
    gce_init_dhcp_hook \
    gce_log_message \
    gce_mount_handler \
    gce_network \
    gce_network.config \
    gce_shutdown \
    gcelogwrapper \
    GceService \
    remoter \
    vnc_server \
    vnc_server-testing \
    simulated_hostapd.conf \
    wpa_supplicant.vsoc.conf \

#
# Packages for AOSP-available stuff we use from the framework
#
PRODUCT_PACKAGES += \
    dhcpcd-6.8.2 \
    dhcpcd-6.8.2.conf \
    e2fsck \
    hostapd \
    ip \
    network \
    perf \
    resize2fs \
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
    device/google/cuttlefish/shared/config/init.vsoc.rc:root/init.vsoc.rc \
    device/google/cuttlefish/shared/config/media_codecs.xml:system/etc/media_codecs.xml \
    device/google/cuttlefish/shared/config/media_codecs_performance.xml:system/etc/media_codecs_performance.xml \
    device/google/cuttlefish/shared/config/media_profiles.xml:system/etc/media_profiles.xml \
    device/google/cuttlefish/shared/config/profile.root:root/profile \
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
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
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
# Device personality files
#
PRODUCT_COPY_FILES += \
    device/google/gce/config/device_personalities/default.json:system/etc/device_personalities/default.json

#
# Files for the VNC server
#
PRODUCT_COPY_FILES += \
    external/libvncserver/webclients/novnc/images/drag.png:system/etc/novnc/images/drag.png \
    external/libvncserver/webclients/novnc/images/screen_700x700.png:system/etc/novnc/images/screen_700x700.png \
    external/libvncserver/webclients/novnc/images/keyboard.png:system/etc/novnc/images/keyboard.png \
    external/libvncserver/webclients/novnc/images/favicon.png:system/etc/novnc/images/favicon.png \
    external/libvncserver/webclients/novnc/images/power.png:system/etc/novnc/images/power.png \
    external/libvncserver/webclients/novnc/images/mouse_none.png:system/etc/novnc/images/mouse_none.png \
    external/libvncserver/webclients/novnc/images/esc.png:system/etc/novnc/images/esc.png \
    external/libvncserver/webclients/novnc/images/connect.png:system/etc/novnc/images/connect.png \
    external/libvncserver/webclients/novnc/images/showextrakeys.png:system/etc/novnc/images/showextrakeys.png \
    external/libvncserver/webclients/novnc/images/mouse_right.png:system/etc/novnc/images/mouse_right.png \
    external/libvncserver/webclients/novnc/images/favicon.ico:system/etc/novnc/images/favicon.ico \
    external/libvncserver/webclients/novnc/images/ctrlaltdel.png:system/etc/novnc/images/ctrlaltdel.png \
    external/libvncserver/webclients/novnc/images/tab.png:system/etc/novnc/images/tab.png \
    external/libvncserver/webclients/novnc/images/mouse_left.png:system/etc/novnc/images/mouse_left.png \
    external/libvncserver/webclients/novnc/images/ctrl.png:system/etc/novnc/images/ctrl.png \
    external/libvncserver/webclients/novnc/images/screen_320x460.png:system/etc/novnc/images/screen_320x460.png \
    external/libvncserver/webclients/novnc/images/alt.png:system/etc/novnc/images/alt.png \
    external/libvncserver/webclients/novnc/images/disconnect.png:system/etc/novnc/images/disconnect.png \
    external/libvncserver/webclients/novnc/images/settings.png:system/etc/novnc/images/settings.png \
    external/libvncserver/webclients/novnc/images/screen_57x57.png:system/etc/novnc/images/screen_57x57.png \
    external/libvncserver/webclients/novnc/images/mouse_middle.png:system/etc/novnc/images/mouse_middle.png \
    external/libvncserver/webclients/novnc/images/clipboard.png:system/etc/novnc/images/clipboard.png \
    external/libvncserver/webclients/novnc/LICENSE.txt:system/etc/novnc/LICENSE.txt \
    external/libvncserver/webclients/novnc/include/display.js:system/etc/novnc/include/display.js \
    external/libvncserver/webclients/novnc/include/des.js:system/etc/novnc/include/des.js \
    external/libvncserver/webclients/novnc/include/Orbitron700.woff:system/etc/novnc/include/Orbitron700.woff \
    external/libvncserver/webclients/novnc/include/websock.js:system/etc/novnc/include/websock.js \
    external/libvncserver/webclients/novnc/include/base64.js:system/etc/novnc/include/base64.js \
    external/libvncserver/webclients/novnc/include/chrome-app/tcp-client.js:system/etc/novnc/include/chrome-app/tcp-client.js \
    external/libvncserver/webclients/novnc/include/keyboard.js:system/etc/novnc/include/keyboard.js \
    external/libvncserver/webclients/novnc/include/util.js:system/etc/novnc/include/util.js \
    external/libvncserver/webclients/novnc/include/jsunzip.js:system/etc/novnc/include/jsunzip.js \
    external/libvncserver/webclients/novnc/include/playback.js:system/etc/novnc/include/playback.js \
    external/libvncserver/webclients/novnc/include/base.css:system/etc/novnc/include/base.css \
    external/libvncserver/webclients/novnc/include/webutil.js:system/etc/novnc/include/webutil.js \
    external/libvncserver/webclients/novnc/include/logo.js:system/etc/novnc/include/logo.js \
    external/libvncserver/webclients/novnc/include/black.css:system/etc/novnc/include/black.css \
    external/libvncserver/webclients/novnc/include/ui.js:system/etc/novnc/include/ui.js \
    external/libvncserver/webclients/novnc/include/keysym.js:system/etc/novnc/include/keysym.js \
    external/libvncserver/webclients/novnc/include/Orbitron700.ttf:system/etc/novnc/include/Orbitron700.ttf \
    external/libvncserver/webclients/novnc/include/web-socket-js/web_socket.js:system/etc/novnc/include/web-socket-js/web_socket.js \
    external/libvncserver/webclients/novnc/include/web-socket-js/WebSocketMain.swf:system/etc/novnc/include/web-socket-js/WebSocketMain.swf \
    external/libvncserver/webclients/novnc/include/web-socket-js/swfobject.js:system/etc/novnc/include/web-socket-js/swfobject.js \
    external/libvncserver/webclients/novnc/include/rfb.js:system/etc/novnc/include/rfb.js \
    external/libvncserver/webclients/novnc/include/vnc.js:system/etc/novnc/include/vnc.js \
    external/libvncserver/webclients/novnc/include/input.js:system/etc/novnc/include/input.js \
    external/libvncserver/webclients/novnc/include/keysymdef.js:system/etc/novnc/include/keysymdef.js \
    external/libvncserver/webclients/novnc/include/blue.css:system/etc/novnc/include/blue.css \
    external/libvncserver/webclients/novnc/vnc_auto.html:system/etc/novnc/vnc_auto.html \
    external/libvncserver/webclients/novnc/vnc.html:system/etc/novnc/vnc.html

# Packages for HAL implementations

#
# Hardware Composer HAL
#
PRODUCT_PACKAGES += \
    hwcomposer.vsoc \
    hwcomposer.vsoc-testing \
    hwcomposer.vsoc-deprecated \
    hwcomposer-stats \
    android.hardware.graphics.composer@2.1-impl

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
    android.hardware.audio.effect@2.0-impl

#
# Drm HAL
#
PRODUCT_PACKAGES += \
    android.hardware.drm@1.0-impl

#
# Dumpstate HAL
#
PRODUCT_PACKAGES += \
    android.hardware.dumpstate@1.0-service.vsoc

#
# Camera
#
PRODUCT_PACKAGES += \
    camera.vsoc \
    camera.vsoc.jpeg \
    camera.device@3.2-impl \
    android.hardware.camera.provider@2.4-impl

#
# GPS
#
PRODUCT_PACKAGES += \
    gps.vsoc \
    android.hardware.gnss@1.0-impl

#
# Sensors
#
PRODUCT_PACKAGES += \
    sensors.vsoc \
    android.hardware.sensors@1.0-impl

#
# Lights
#
PRODUCT_PACKAGES += \
    lights.vsoc \
    android.hardware.light@2.0-impl

#
# Keymaster HAL
#
PRODUCT_PACKAGES += \
     android.hardware.keymaster@3.0-impl

#
# Power HAL
#
PRODUCT_PACKAGES += \
    power.vsoc \
    android.hardware.power@1.0-impl

# TODO vibrator HAL
# TODO thermal

