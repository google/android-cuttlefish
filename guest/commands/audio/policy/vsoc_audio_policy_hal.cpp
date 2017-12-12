/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <system/audio_policy.h>
#include <hardware/audio_policy.h>

#include "guest/commands/audio/policy/vsoc_audio_policy_hal.h"

namespace avd {

int GceAudioPolicy::Create(
    const audio_policy_device* device,
    audio_policy_service_ops* aps_ops,
    void* service, audio_policy** ap) {
  D("%s", __FUNCTION__);
  audio_policy_device* dev;
  gce_audio_policy* dap;
  int ret;

  *ap = NULL;

  if (!service || !aps_ops) {
    return -EINVAL;
  }

  dap = (gce_audio_policy*) calloc(1, sizeof(*dap));
  if (!dap) {
    return -ENOMEM;
  }

  dap->policy.set_device_connection_state =
      &GceAudioPolicy::SetDeviceConnectionState;
  dap->policy.get_device_connection_state =
      &GceAudioPolicy::GetDeviceConnectionState;
  dap->policy.set_phone_state = &GceAudioPolicy::SetPhoneState;
  dap->policy.set_ringer_mode = &GceAudioPolicy::SetRingerMode;
  dap->policy.set_force_use = &GceAudioPolicy::SetForceUse;
  dap->policy.get_force_use = &GceAudioPolicy::GetForceUse;
  dap->policy.set_can_mute_enforced_audible =
      &GceAudioPolicy::SetCanMuteEnforcedAudible;
  dap->policy.init_check = &GceAudioPolicy::InitCheck;
  dap->policy.get_output = &GceAudioPolicy::GetOutput;
  dap->policy.start_output = &GceAudioPolicy::StartOutput;
  dap->policy.stop_output = &GceAudioPolicy::StopOutput;
  dap->policy.release_output = &GceAudioPolicy::ReleaseOutput;
  dap->policy.get_input = &GceAudioPolicy::GetInput;
  dap->policy.start_input = &GceAudioPolicy::StartInput;
  dap->policy.stop_input = &GceAudioPolicy::StopInput;
  dap->policy.release_input = &GceAudioPolicy::ReleaseInput;
  dap->policy.init_stream_volume = &GceAudioPolicy::InitStreamVolume;
  dap->policy.set_stream_volume_index = &GceAudioPolicy::SetStreamVolumeIndex;
  dap->policy.get_stream_volume_index = &GceAudioPolicy::GetStreamVolumeIndex;
  dap->policy.set_stream_volume_index_for_device =
      &GceAudioPolicy::SetStreamVolumeIndexForDevice;
  dap->policy.get_stream_volume_index_for_device =
      &GceAudioPolicy::GetStreamVolumeIndexForDevice;
  dap->policy.get_strategy_for_stream = &GceAudioPolicy::GetStrategyForStream;
  dap->policy.get_devices_for_stream = &GceAudioPolicy::GetDevicesForStream;
  dap->policy.get_output_for_effect = &GceAudioPolicy::GetOutputForEffect;
  dap->policy.register_effect = &GceAudioPolicy::RegisterEffect;
  dap->policy.unregister_effect = &GceAudioPolicy::UnregisterEffect;
  dap->policy.set_effect_enabled = &GceAudioPolicy::SetEffectEnabled;
  dap->policy.is_stream_active = &GceAudioPolicy::IsStreamActive;
  dap->policy.dump = &GceAudioPolicy::Dump;
#ifdef ENABLE_OFFLOAD
  dap->policy.is_offload_supported = &GceAudioPolicy::IsOffloadSupported;
#endif

  dap->service = service;
  dap->aps_ops = aps_ops;

  *ap = &dap->policy;
  return 0;
}


int GceAudioPolicy::Destroy(const audio_policy_device* ap_dev,
                            audio_policy* ap) {
  D("%s", __FUNCTION__);
  free(ap);
  return 0;
}


int GceAudioPolicy::Close(hw_device_t* device) {
  D("%s", __FUNCTION__);
  free(device);
  return 0;
}


int GceAudioPolicy::Open(
    const hw_module_t* module, const char* name, hw_device_t** device) {
  D("%s", __FUNCTION__);
  audio_policy_device* dev;

  *device = NULL;

  if (strcmp(name, AUDIO_POLICY_INTERFACE) != 0) {
    return -EINVAL;
  }

  dev = (audio_policy_device*) calloc(1, sizeof(*dev));
  if (!dev) {
    return -ENOMEM;
  }

  dev->common.tag = HARDWARE_DEVICE_TAG;
  dev->common.version = 0;
  dev->common.module = (hw_module_t*) module;
  dev->common.close = &GceAudioPolicy::Close;
  dev->create_audio_policy = &GceAudioPolicy::Create;
  dev->destroy_audio_policy = &GceAudioPolicy::Destroy;

  *device = &dev->common;

  return 0;
}

}

static hw_module_methods_t gce_audio_policy_module_methods = {
  .open = &avd::GceAudioPolicy::Open,
};


audio_policy_module HAL_MODULE_INFO_SYM = {
  .common = {
    .tag           = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id            = AUDIO_POLICY_HARDWARE_MODULE_ID,
    .name          = "GCE Audio Policy HAL",
    .author        = "The Android Open Source Project",
    .methods       = &gce_audio_policy_module_methods,
  },
};
