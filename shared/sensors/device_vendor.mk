#
# Copyright (C) 2023 The Android Open Source Project
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

# set LOCAL_SENSOR_FILE_OVERRIDES := true if a device has a custom list of sensors. Otherwise
# install the default set like below
ifneq ($(LOCAL_SENSOR_FILE_OVERRIDES),true)
    PRODUCT_COPY_FILES += \
        frameworks/native/data/etc/android.hardware.sensor.ambient_temperature.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.ambient_temperature.xml \
        frameworks/native/data/etc/android.hardware.sensor.barometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.barometer.xml \
        frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
        frameworks/native/data/etc/android.hardware.sensor.hinge_angle.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.hinge_angle.xml \
        frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
        frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
        frameworks/native/data/etc/android.hardware.sensor.relative_humidity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.relative_humidity.xml
endif

PRODUCT_SOONG_NAMESPACES += device/google/cuttlefish/shared/sensors/multihal

# Set LOCAL_SENSOR_PRODUCT_PACKAGE := <package list> if a device wants to install custom implementations
# Should check if the default feature list is okay with the implementation. Otherwise, it should set
# LOCAL_SENSOR_FILE_OVERRIDES and copy feature files.
ifeq ($(LOCAL_SENSOR_PRODUCT_PACKAGE),)
       LOCAL_SENSOR_PRODUCT_PACKAGE := com.android.hardware.sensors
endif
PRODUCT_PACKAGES += \
    $(LOCAL_SENSOR_PRODUCT_PACKAGE)

PRODUCT_PACKAGES += \
    cuttlefish_sensor_injection