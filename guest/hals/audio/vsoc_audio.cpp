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
#include "guest/hals/audio/audio_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>

extern "C" {
#include <cutils/str_parms.h>
}

#include "common/libs/auto_resources/auto_resources.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/threads/cuttlefish_thread.h"
#include "common/libs/threads/thunkers.h"
#include "common/vsoc/lib/circqueue_impl.h"
#include "guest/hals/audio/vsoc_audio.h"
#include "guest/hals/audio/vsoc_audio_input_stream.h"
#include "guest/hals/audio/vsoc_audio_output_stream.h"
#include "guest/libs/platform_support/api_level_fixes.h"
#include "guest/libs/remoter/remoter_framework_pkt.h"

using cvd::LockGuard;
using cvd::Mutex;

namespace cvd {

namespace {
template <typename F> struct HWDeviceThunker :
  ThunkerBase<hw_device_t, GceAudio, F>{};
template <typename F> struct AudioThunker :
  ThunkerBase<audio_hw_device, GceAudio, F>{};
template <typename F> struct AudioThreadThunker :
  ThunkerBase<void, GceAudio, F>{};
}

GceAudio::~GceAudio() { }

int GceAudio::Close() {
  D("GceAudio::%s", __FUNCTION__);
  {
    LockGuard<Mutex> guard(lock_);
    for (std::list<GceAudioOutputStream*>::iterator it = output_list_.begin();
         it != output_list_.end(); ++it) {
      delete *it;
    }
    for (input_map_t::iterator it = input_map_.begin();
         it != input_map_.end(); ++it) {
      delete it->second;
    }
  }
  delete this;
  return 0;
}

size_t GceAudio::GetInputBufferSize(const audio_config*) const {
  return IN_BUFFER_BYTES;
}

uint32_t GceAudio::GetSupportedDevices() const {
  return AUDIO_DEVICE_OUT_EARPIECE |
      AUDIO_DEVICE_OUT_SPEAKER |
      AUDIO_DEVICE_OUT_DEFAULT |
      AUDIO_DEVICE_IN_COMMUNICATION |
      AUDIO_DEVICE_IN_BUILTIN_MIC |
      AUDIO_DEVICE_IN_WIRED_HEADSET |
      AUDIO_DEVICE_IN_VOICE_CALL |
      AUDIO_DEVICE_IN_DEFAULT;
}

int GceAudio::InitCheck() const {
  D("GceAudio::%s", __FUNCTION__);
  return 0;
}

int GceAudio::SetMicMute(bool state) {
  D("GceAudio::%s", __FUNCTION__);
  LockGuard<Mutex> guard(lock_);
  mic_muted_ = state;
  return 0;
}

int GceAudio::GetMicMute(bool *state) const {
  D("GceAudio::%s", __FUNCTION__);
  LockGuard<Mutex> guard(lock_);
  *state = mic_muted_;
  return 0;
}

int GceAudio::OpenInputStream(audio_io_handle_t handle,
                              audio_devices_t devices,
                              audio_config *config,
                              audio_stream_in **stream_in,
                              audio_input_flags_t /*flags*/,
                              const char * /*address*/,
                              audio_source_t /*source*/) {
  GceAudioInputStream* new_stream;
  int rval = GceAudioInputStream::Open(
      this, handle, devices, *config, &new_stream);
  uint32_t stream_number;
  if (new_stream) {
    LockGuard<Mutex> guard(lock_);
    stream_number = next_stream_number_++;
    input_map_[stream_number] = new_stream;
  }
  // This should happen after the lock is released, hence the double check
  if (new_stream) {
    SendStreamUpdate(new_stream->GetStreamDescriptor(
        stream_number, gce_audio_message::OPEN_INPUT_STREAM), MSG_DONTWAIT);
  }
  *stream_in = new_stream;
  return rval;
}


void GceAudio::CloseInputStream(audio_stream_in *stream) {
  GceAudioInputStream* astream = static_cast<GceAudioInputStream*>(stream);
  gce_audio_message descriptor;
  {
    LockGuard<Mutex> guard(lock_);
    // TODO(ghartman): This could be optimized if stream knew it's number.
    for (input_map_t::iterator it = input_map_.begin();
         it != input_map_.end(); ++it) {
      if (it->second == stream) {
        descriptor = it->second->GetStreamDescriptor(
            it->first, gce_audio_message::CLOSE_INPUT_STREAM);
        input_map_.erase(it);
        break;
      }
    }
  }
  SendStreamUpdate(descriptor, MSG_DONTWAIT);
  delete astream;
}


int GceAudio::OpenOutputStream(audio_io_handle_t handle,
                               audio_devices_t devices,
                               audio_output_flags_t flags,
                               audio_config *config,
                               audio_stream_out **stream_out,
                               const char * /*address*/) {
  GceAudioOutputStream* new_stream;
  int rval;
  {
    LockGuard<Mutex> guard(lock_);
    rval = GceAudioOutputStream::Open(
        this, handle, devices, flags, config, next_stream_number_++,
        &new_stream);
    if (new_stream) {
      output_list_.push_back(new_stream);
    }
  }
  if (new_stream) {
    SendStreamUpdate(new_stream->GetStreamDescriptor(
        gce_audio_message::OPEN_OUTPUT_STREAM), MSG_DONTWAIT);
  }
  *stream_out = new_stream;
  return rval;
}

void GceAudio::CloseOutputStream(audio_stream_out *stream) {
  GceAudioOutputStream* astream = static_cast<GceAudioOutputStream*>(stream);
  gce_audio_message close;
  {
    LockGuard<Mutex> guard(lock_);
    output_list_.remove(astream);
    close = astream->GetStreamDescriptor(
        gce_audio_message::CLOSE_OUTPUT_STREAM);
  }
  SendStreamUpdate(close, MSG_DONTWAIT);
  delete astream;
}

int GceAudio::Dump(int fd) const {
  LockGuard<Mutex> guard(lock_);
  VSOC_FDPRINTF(
      fd,
      "\nadev_dump:\n"
      "\tmic_mute: %s\n"
      "\tnum_outputs: %zu\n"
      "\tnum_inputs: %zu\n\n",
      mic_muted_ ? "true": "false",
      output_list_.size(), input_map_.size());

  for (std::list<GceAudioOutputStream*>::const_iterator it =
           output_list_.begin();
       it != output_list_.end(); ++it) {
    (*it)->common.dump(&(*it)->common, fd);
  }

  for (input_map_t::const_iterator it = input_map_.begin();
       it != input_map_.end(); ++it) {
    (*it).second->common.dump(&(*it).second->common, fd);
  }

  return 0;
}

ssize_t GceAudio::SendMsg(const msghdr& msg, int /* flags */) {
    intptr_t res = audio_data_rv_->data()->audio_queue.Writev(
            audio_data_rv_,
            msg.msg_iov,
            msg.msg_iovlen,
            true /* non_blocking */);

    if (res < 0) {
        ALOGV("GceAudio::%s: CircularPacketQueue::Write returned %" PRIiPTR,
              __FUNCTION__,
              res);
    }

    return static_cast<ssize_t>(res);
}

ssize_t GceAudio::SendStreamUpdate(
    const gce_audio_message& stream_info, int flags) {
  msghdr msg;
  iovec msg_iov[1];
  msg_iov[0].iov_base = const_cast<gce_audio_message*>(&stream_info);
  msg_iov[0].iov_len = sizeof(gce_audio_message);
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = msg_iov;
  msg.msg_iovlen = arraysize(msg_iov);
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
  return SendMsg(msg, flags);
}

int GceAudio::SetVoiceVolume(float volume) {
  D("GceAudio::%s: set voice volume %f", __FUNCTION__, volume);
  voice_volume_ = volume;
  return 0;
}

int GceAudio::SetMasterVolume(float volume) {
  D("GceAudio::%s: set master volume %f", __FUNCTION__, volume);
  master_volume_ = volume;
  return 0;
}

int GceAudio::GetMasterVolume(float* volume) {
  D("GceAudio::%s: get master volume %f", __FUNCTION__, master_volume_);
  *volume = master_volume_;
  return 0;
}

int GceAudio::SetMasterMute(bool muted) {
  D("GceAudio::%s: set master muted %d", __FUNCTION__, muted);
  master_muted_ = muted;
  return 0;
}

int GceAudio::GetMasterMute(bool* muted) {
  D("GceAudio::%s: get master muted %d", __FUNCTION__, master_muted_);
  *muted = master_muted_;
  return 0;
}

int GceAudio::SetMode(audio_mode_t mode) {
  D("GceAudio::%s: new mode %d", __FUNCTION__, mode);
  mode_ = mode;
  return 0;
}

int GceAudio::Open(const hw_module_t* module, const char* name,
                   hw_device_t** device) {
  D("GceAudio::%s", __FUNCTION__);

  if (strcmp(name, AUDIO_HARDWARE_INTERFACE)) {
    ALOGE("GceAudio::%s: invalid module name %s (expected %s)",
          __FUNCTION__, name, AUDIO_HARDWARE_INTERFACE);
    return -EINVAL;
  }

  GceAudio* rval = new GceAudio;

  rval->audio_data_rv_ = AudioDataRegionView::GetInstance();
  rval->audio_data_rv_->Open();
  rval->audio_worker_ = rval->audio_data_rv_->StartWorker();

  rval->common.tag = HARDWARE_DEVICE_TAG;
  rval->common.version = version_;
  rval->common.module = const_cast<hw_module_t *>(module);
  rval->common.close = HWDeviceThunker<int()>::call<&GceAudio::Close>;

#if !defined(AUDIO_DEVICE_API_VERSION_2_0)
  // This HAL entry is supported only on AUDIO_DEVICE_API_VERSION_1_0.
  // In fact, with version 2.0 the device numbers were orgainized in a
  // way that makes the return value nonsense.
  // Skipping the assignment is ok: the memset in the constructor already
  // put a NULL here.
  rval->get_supported_devices =
      AudioThunker<uint32_t()>::call<&GceAudio::GetSupportedDevices>;
#endif
  rval->init_check = AudioThunker<int()>::call<&GceAudio::InitCheck>;

  rval->set_voice_volume =
      AudioThunker<int(float)>::call<&GceAudio::SetVoiceVolume>;
  rval->set_master_volume =
      AudioThunker<int(float)>::call<&GceAudio::SetMasterVolume>;
  rval->get_master_volume =
      AudioThunker<int(float*)>::call<&GceAudio::GetMasterVolume>;

#if defined(AUDIO_DEVICE_API_VERSION_2_0)
  rval->set_master_mute =
      AudioThunker<int(bool)>::call<&GceAudio::SetMasterMute>;
  rval->get_master_mute =
      AudioThunker<int(bool*)>::call<&GceAudio::GetMasterMute>;
#endif

  rval->set_mode = AudioThunker<int(audio_mode_t)>::call<&GceAudio::SetMode>;
  rval->set_mic_mute = AudioThunker<int(bool)>::call<&GceAudio::SetMicMute>;
  rval->get_mic_mute =
      AudioThunker<int(bool*)>::call<&GceAudio::GetMicMute>;

  rval->set_parameters =
      AudioThunker<int(const char*)>::call<&GceAudio::SetParameters>;
  rval->get_parameters =
      AudioThunker<char*(const char*)>::call<&GceAudio::GetParameters>;

  rval->get_input_buffer_size =
      AudioThunker<size_t(const audio_config*)>::call<
        &GceAudio::GetInputBufferSize>;

  rval->open_input_stream =
      AudioThunker<GceAudio::OpenInputStreamHAL_t>::call<
        &GceAudio::OpenInputStreamCurrentHAL>;
  rval->close_input_stream =
      AudioThunker<void(audio_stream_in*)>::call<&GceAudio::CloseInputStream>;

  rval->open_output_stream =
      AudioThunker<GceAudio::OpenOutputStreamHAL_t>::call<
        &GceAudio::OpenOutputStreamCurrentHAL>;
  rval->close_output_stream =
      AudioThunker<void(audio_stream_out*)>::call<&GceAudio::CloseOutputStream>;

  rval->dump = AudioThunker<int(int)>::call<&GceAudio::Dump>;

  *device = &rval->common;
  return 0;
}

int GceAudio::SetParameters(const char *kvpairs) {
  ALOGE("GceAudio::%s: not implemented", __FUNCTION__);
  if (kvpairs) D("GceAudio::%s: kvpairs %s", __FUNCTION__, kvpairs);
  return 0;
}


char* GceAudio::GetParameters(const char *keys) const {
  ALOGE("GceAudio::%s: not implemented", __FUNCTION__);
  if (keys) D("GceAudio::%s: kvpairs %s", __FUNCTION__, keys);
  return strdup("");
}

int GceAudio::SetStreamParameters(
    struct audio_stream *stream, const char *kv_pairs) {
  struct str_parms *parms = str_parms_create_str(kv_pairs);
  if (!parms) {
    return 0;
  }
  int sample_rate;
  if (str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_SAMPLING_RATE,
                        &sample_rate) >= 0) {
    stream->set_sample_rate(stream, sample_rate);
  }
  int format;
  if (str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_FORMAT,
                        &format) >= 0) {
    stream->set_format(stream, static_cast<audio_format_t>(format));
  }
  int routing;
  if (str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                        &routing) >= 0) {
    stream->set_device(stream, static_cast<audio_devices_t>(routing));
  }
  if (str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE,
                        &routing) >= 0) {
    stream->set_device(stream, static_cast<audio_devices_t>(routing));
  }
  str_parms_destroy(parms);
  return 0;
}

}
