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
#pragma once

#include <errno.h>
#include <string.h>
#include <cutils/log.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#define AUDIO_DEBUG 1

#if AUDIO_DEBUG
#  define D(...) ALOGD(__VA_ARGS__)
#else
#  define D(...) ((void)0)
#endif

#define LOG_TAG "GceAudioPolicy"

namespace avd {

struct gce_audio_policy {
  audio_policy policy;

  audio_policy_service_ops* aps_ops;
  void* service;
};


class GceAudioPolicy {
 public:
  GceAudioPolicy() {}
  ~GceAudioPolicy() {}

  static int Open(
      const hw_module_t* module, const char* name, hw_device_t** device);
  static int Create(const audio_policy_device* device,
      audio_policy_service_ops* aps_ops, void* service,
      audio_policy** ap);
  static int Destroy(const audio_policy_device* ap_dev,
                     audio_policy* ap);
  static int Close(hw_device_t* device);

  static int SetDeviceConnectionState(
      audio_policy* pol, audio_devices_t device,
      audio_policy_dev_state_t state, const char* device_address) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static audio_policy_dev_state_t GetDeviceConnectionState(
      const audio_policy* pol, audio_devices_t device,
      const char* device_address) {
    ALOGE("%s: not supported", __FUNCTION__);
    return AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE;
  }

  static void SetPhoneState(audio_policy* pol, audio_mode_t state) {
    ALOGE("%s: not supported", __FUNCTION__);
  }

  static void SetRingerMode(audio_policy* pol, uint32_t mode,
                            uint32_t mask) {
    ALOGW("%s: deprecated", __FUNCTION__);
  }

  static void SetForceUse(
      audio_policy* pol, audio_policy_force_use_t usage,
    audio_policy_forced_cfg_t config) {
    ALOGE("%s: not supported", __FUNCTION__);
  }

  static audio_policy_forced_cfg_t GetForceUse(
      const audio_policy* pol, audio_policy_force_use_t usage) {
    ALOGE("%s: not supported", __FUNCTION__);
    return AUDIO_POLICY_FORCE_NONE;
  }

  static void SetCanMuteEnforcedAudible(
      audio_policy* pol, bool can_mute) {
    ALOGE("%s: not supported", __FUNCTION__);
  }

  static int InitCheck(const audio_policy* pol) {
    ALOGE("%s: not supported", __FUNCTION__);
    return 0;
  }

  static audio_io_handle_t GetOutput(
      audio_policy* pol, audio_stream_type_t stream,
      uint32_t sampling_rate, audio_format_t format,
      audio_channel_mask_t channelMask, audio_output_flags_t flags
#ifdef ENABLE_OFFLOAD
      , const audio_offload_info_t* info
#endif
      ) {
    ALOGE("%s: not supported", __FUNCTION__);
    return 0;
  }

  static int StartOutput(audio_policy* pol, audio_io_handle_t output,
                         audio_stream_type_t stream, int session) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static int StopOutput(audio_policy* pol, audio_io_handle_t output,
                        audio_stream_type_t stream, int session) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static void ReleaseOutput(
      audio_policy* pol, audio_io_handle_t output) {
    ALOGE("%s: not supported", __FUNCTION__);
  }

  static audio_io_handle_t GetInput(
      audio_policy* pol, audio_source_t inputSource,
      uint32_t sampling_rate, audio_format_t format,
      audio_channel_mask_t channelMask, audio_in_acoustics_t acoustics) {
    ALOGE("%s: not supported", __FUNCTION__);
    return 0;
  }

  static int StartInput(audio_policy* pol, audio_io_handle_t input) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static int StopInput(audio_policy* pol, audio_io_handle_t input) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static void ReleaseInput(
      audio_policy* pol, audio_io_handle_t input) {
    ALOGE("%s: not supported", __FUNCTION__);
  }

  static void InitStreamVolume(audio_policy* pol,
                               audio_stream_type_t stream, int index_min,
                               int index_max) {
    ALOGE("%s: not supported", __FUNCTION__);
  }

  static int SetStreamVolumeIndex(audio_policy* pol,
                                  audio_stream_type_t stream, int index) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static int GetStreamVolumeIndex(const audio_policy* pol,
                                  audio_stream_type_t stream, int* index) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static int SetStreamVolumeIndexForDevice(
      audio_policy* pol, audio_stream_type_t stream,
      int index, audio_devices_t device) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static int GetStreamVolumeIndexForDevice(
      const audio_policy* pol, audio_stream_type_t stream,
      int* index, audio_devices_t device) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static uint32_t GetStrategyForStream(const audio_policy* pol,
                                       audio_stream_type_t stream) {
    ALOGE("%s: not supported", __FUNCTION__);
    return 0;
  }

  static audio_devices_t GetDevicesForStream(const audio_policy* pol,
                                             audio_stream_type_t stream) {
    ALOGE("%s: not supported", __FUNCTION__);
    return 0;
  }

  static audio_io_handle_t GetOutputForEffect(
      audio_policy* pol, const effect_descriptor_s* desc) {
    ALOGE("%s: not supported", __FUNCTION__);
    return 0;
  }

  static int RegisterEffect(
      audio_policy* pol, const effect_descriptor_s* desc,
      audio_io_handle_t output, uint32_t strategy, int session, int id) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static int UnregisterEffect(audio_policy* pol, int id) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static int SetEffectEnabled(audio_policy* pol, int id, bool enabled) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

  static bool IsStreamActive(
      const audio_policy* pol, audio_stream_type_t stream,
      uint32_t in_past_ms) {
    ALOGE("%s: not supported", __FUNCTION__);
    return false;
  }

  static int Dump(const audio_policy* pol, int fd) {
    ALOGE("%s: not supported", __FUNCTION__);
    return -ENOSYS;
  }

#ifdef ENABLE_OFFLOAD
  static bool IsOffloadSupported(const audio_policy* pol,
                                 const audio_offload_info_t* info) {
    ALOGE("%s: not supported", __FUNCTION__);
    return false;
  }
#endif
};

}
