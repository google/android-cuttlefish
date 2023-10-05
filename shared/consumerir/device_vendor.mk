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

# TODO(b/302088370) remove the condition when libapexsupport is available
ifeq ($(RELEASE_AIDL_USE_UNFROZEN),true)
PRODUCT_PACKAGES += \
    com.google.cf.ir
else
PRODUCT_PACKAGES += \
    android.hardware.ir-service.example \
    consumerir.default
endif # RELEASE_AIDL_USE_UNFROZEN
