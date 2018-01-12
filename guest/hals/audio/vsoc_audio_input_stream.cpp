/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include <stdio.h>
#include <stdlib.h>
#include <cstdint>

extern "C"{
#include <cutils/str_parms.h>
}

#include "common/libs/auto_resources/auto_resources.h"
#include "common/libs/threads/thunkers.h"
#include "guest/hals/audio/audio_hal.h"
#include "guest/hals/audio/vsoc_audio.h"
#include "guest/hals/audio/vsoc_audio_input_stream.h"
#include "guest/libs/platform_support/api_level_fixes.h"

namespace cvd {

namespace {
template <typename F> struct Thunker :
  ThunkerBase<audio_stream, GceAudioInputStream, F>{};
template <typename F> struct InThunker :
  ThunkerBase<audio_stream_in, GceAudioInputStream, F>{};
}

#if defined(AUDIO_DEVICE_API_VERSION_3_0)
static inline size_t GceAudioFrameSize(const audio_stream_in* s) {
  return audio_stream_in_frame_size(s);
}
#elif defined(AUDIO_DEVICE_API_VERSION_2_0)
static inline size_t GceAudioFrameSize(const audio_stream_in* s) {

  return audio_stream_frame_size(&s->common);
}
#else
static inline size_t GceAudioFrameSize(audio_stream_in* s) {

  return audio_stream_frame_size(&s->common);
}
#endif

GceAudioInputStream::GceAudioInputStream(
    cvd::GceAudio* dev, audio_devices_t devices, const audio_config& config)
    : audio_stream_in(),
      dev_(dev),
      config_(config),
      gain_(0.0),
      device_(devices) {
  common.get_sample_rate =
      Thunker<uint32_t()>::call<&GceAudioInputStream::GetSampleRate>;
  common.set_sample_rate =
      Thunker<int(uint32_t)>::call<&GceAudioInputStream::SetSampleRate>;
  common.get_buffer_size =
      Thunker<size_t()>::call<&GceAudioInputStream::GetBufferSize>;
  common.get_channels =
      Thunker<audio_channel_mask_t()>::call<&GceAudioInputStream::GetChannels>;
  common.get_device =
      Thunker<audio_devices_t()>::call<&GceAudioInputStream::GetDevice>;
  common.set_device =
      Thunker<int(audio_devices_t)>::call<&GceAudioInputStream::SetDevice>;
  common.get_format =
      Thunker<audio_format_t()>::call<&GceAudioInputStream::GetFormat>;
  common.set_format =
      Thunker<int(audio_format_t)>::call<&GceAudioInputStream::SetFormat>;
  common.standby =
      Thunker<int()>::call<&GceAudioInputStream::Standby>;
  common.dump =
      Thunker<int(int)>::call<&GceAudioInputStream::Dump>;
  common.set_parameters = GceAudio::SetStreamParameters;
  common.get_parameters =
      Thunker<char*(const char *)>::call<&GceAudioInputStream::GetParameters>;
  common.add_audio_effect =
      Thunker<int(effect_handle_t)>::call<&GceAudioInputStream::AddAudioEffect>;
  common.remove_audio_effect = Thunker<int(effect_handle_t)>::call<
    &GceAudioInputStream::RemoveAudioEffect>;
  set_gain = InThunker<int(float)>::call<&GceAudioInputStream::SetGain>;
  read = InThunker<ssize_t(void*, size_t)>::call<
    &GceAudioInputStream::Read>;
  get_input_frames_lost = InThunker<uint32_t()>::call<
    &GceAudioInputStream::GetInputFramesLost>;
  frame_size_ = GceAudioFrameSize(this);
  buffer_model_.reset(
      new SimulatedInputBuffer(config_.sample_rate, GetBufferSize() /
                               frame_size_));
  reported_lost_frames_ = 0;
}

gce_audio_message GceAudioInputStream::GetStreamDescriptor(
    uint32_t stream_number, gce_audio_message::message_t event) {
  gce_audio_message rval;
  rval.message_type = event;
  rval.stream_number = stream_number;
  rval.frame_num = buffer_model_->GetCurrentItemNum();
  rval.time_presented =
      buffer_model_->GetLastUpdatedTime().SinceEpoch().GetTS();
  rval.frame_rate = config_.sample_rate;
  rval.channel_mask = config_.channel_mask;
  rval.format = config_.format;
  rval.frame_size = frame_size_;
  return rval;
}

int GceAudioInputStream::Open(GceAudio* dev,
                              audio_io_handle_t /*handle*/,
                              audio_devices_t devices,
                              const audio_config& config,
                              GceAudioInputStream** stream_in) {
  D("GceAudioInputStream::%s", __FUNCTION__);
  *stream_in = new GceAudioInputStream(dev, devices, config);
  return 0;
}

int GceAudioInputStream::SetFormat(audio_format_t format) {
  config_.format = format;
  frame_size_ = GceAudioFrameSize(this);
  return 0;
}

int GceAudioInputStream::Dump(int fd) const {
  D("GceAudioInputStream::%s", __FUNCTION__);
  VSOC_FDPRINTF(
      fd,
      "\tInputSteam Dump:\n"
      "\t\tsample rate: %u\n"
      "\t\tbuffer size: %zu\n"
      "\t\tchannel mask: %08x\n"
      "\t\tformat: %d\n"
      "\t\tdevice: %08x\n"
      "\t\taudio dev: %p\n\n",
      GetSampleRate(), GetBufferSize(),
      GetChannels(), GetFormat(), device_, dev_);
  return 0;
}

int GceAudioInputStream::SetSampleRate(uint32_t sample_rate) {
  if (sample_rate != config_.sample_rate) {
    config_.sample_rate = sample_rate;
    buffer_model_.reset(
        new SimulatedInputBuffer(sample_rate, GetBufferSize() / frame_size_));
    reported_lost_frames_ = 0;
  }
  return 0;
}

char* GceAudioInputStream::GetParameters(const char* keys) const {
  D("GceAudioInputStream::%s", __FUNCTION__);
  if (keys) D("GceAudioInputStream::%s keys %s", __FUNCTION__, keys);

  str_parms* query = str_parms_create_str(keys);
  str_parms* reply = str_parms_create();

  char value[256];
  int ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_ROUTING,
                              value, sizeof(value));
  char* str;
  if (ret >= 0) {
    str_parms_add_int(reply, AUDIO_PARAMETER_STREAM_ROUTING, device_);
    str = strdup(str_parms_to_str(reply));
  } else {
    str = strdup(keys);
  }
  str_parms_destroy(query);
  str_parms_destroy(reply);
  return str;
}


ssize_t GceAudioInputStream::Read(void* buffer, size_t bytes) {
  int64_t available = buffer_model_->RemoveFromInputBuffer(
      bytes / frame_size_, false) * frame_size_;
  ssize_t rval = available;
  if ((rval != available) || (rval < 0)) {
    ALOGE("GceAudioInputStream:%s got bad value from "
          "RemoveFromInputBuffer %" PRId64, __FUNCTION__, available);
    return -1;
  }
  memset(buffer, 0, rval);
  return rval;
}

}
