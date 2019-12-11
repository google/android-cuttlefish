# Copyright 2017 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?))
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := tests

# Only compile source java files in this apk.
LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_PACKAGE_NAME := CuttlefishWifiTests
LOCAL_SDK_VERSION := current
LOCAL_CERTIFICATE := platform
ifeq (0, $(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?))
LOCAL_JAVA_LIBRARIES := android.test.runner.stubs
else
LOCAL_JAVA_LIBRARIES := android.test.runner
endif
LOCAL_STATIC_JAVA_LIBRARIES := android-support-test platform-test-annotations

include $(BUILD_PACKAGE)
endif
