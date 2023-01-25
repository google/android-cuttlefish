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

$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,.idc,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,default-permissions.xml,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,libnfc-nci.conf,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,fstab.postinstall,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,ueventd.rc,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,wpa_supplicant.conf,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,hals.conf,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,device_state_configuration.xml,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,task_profiles.json,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,p2p_supplicant.conf,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,wpa_supplicant.conf,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,wpa_supplicant_overlay.conf,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,wpa_supplicant.rc,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,init.cutf_cvm.rc,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,bt_vhci_forwarder.rc,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,fstab.cf.f2fs.hctr2,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,fstab.cf.f2fs.cts,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,fstab.cf.ext4.hctr2,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,fstab.cf.ext4.cts,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,init.rc,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))
$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish,audio_policy.conf,SPDX-license-identifier-Apache-2.0,notice,build/soong/licenses/LICENSE,))

$(eval $(call declare-copy-files-license-metadata,device/google/cuttlefish/shared/config,pci.ids,SPDX-license-identifier-BSD-3-Clause,notice,device/google/cuttlefish/shared/config/LICENSE_BSD,))

$(eval $(call declare-1p-copy-files,device/google/cuttlefish,privapp-permissions-cuttlefish.xml))
$(eval $(call declare-1p-copy-files,device/google/cuttlefish,media_profiles_V1_0.xml))
$(eval $(call declare-1p-copy-files,device/google/cuttlefish,media_codecs_performance.xml))
$(eval $(call declare-1p-copy-files,device/google/cuttlefish,cuttlefish_excluded_hardware.xml))
$(eval $(call declare-1p-copy-files,device/google/cuttlefish,media_codecs.xml))
$(eval $(call declare-1p-copy-files,device/google/cuttlefish,media_codecs_google_video.xml))
$(eval $(call declare-1p-copy-files,device/google/cuttlefish,car_audio_configuration.xml))
$(eval $(call declare-1p-copy-files,device/google/cuttlefish,audio_policy_configuration.xml))
$(eval $(call declare-1p-copy-files,device/google/cuttlefish,preinstalled-packages-product-car-cuttlefish.xml))
$(eval $(call declare-1p-copy-files,hardware/google/camera/devices,.json))

ifneq ($(filter vsoc_arm vsoc_arm64 vsoc_riscv64 vsoc_x86 vsoc_x86_64, $(TARGET_BOARD_PLATFORM)),)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include $(LOCAL_PATH)/fetcher.mk

include $(CLEAR_VARS)
include $(LOCAL_PATH)/host_package.mk

endif

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include $(call all-makefiles-under,$(LOCAL_PATH))
