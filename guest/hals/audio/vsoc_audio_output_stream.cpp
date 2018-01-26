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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory>

#include <cutils/sockets.h>
extern "C"{
#include <cutils/str_parms.h>
}

#include "common/libs/auto_resources/auto_resources.h"
#include "common/libs/threads/thunkers.h"
#include "common/libs/time/monotonic_time.h"
#include "guest/hals/audio/audio_hal.h"
#include "guest/hals/audio/vsoc_audio.h"
#include "guest/hals/audio/vsoc_audio_output_stream.h"
#include "guest/libs/platform_support/api_level_fixes.h"
#include "guest/libs/remoter/remoter_framework_pkt.h"

#if defined(AUDIO_DEVICE_API_VERSION_3_0)
static inline size_t GceAudioFrameSize(const audio_stream_out* s) {
  return audio_stream_out_frame_size(s);
}
#elif defined(AUDIO_DEVICE_API_VERSION_2_0)
static inline size_t GceAudioFrameSize(const audio_stream_out* s) {

  return audio_stream_frame_size(&s->common);
}
#else
static inline size_t GceAudioFrameSize(audio_stream_out* s) {

  return audio_stream_frame_size(&s->common);
}
#endif

namespace cvd {

const size_t GceAudioOutputStream::kOutBufferSize;
const size_t GceAudioOutputStream::kOutLatency;

namespace {
template <typename F> struct Thunker :
  ThunkerBase<audio_stream, GceAudioOutputStream, F>{};

template <typename F> struct OutThunker :
  ThunkerBase<audio_stream_out, GceAudioOutputStream, F>{};
}

GceAudioOutputStream::GceAudioOutputStream(GceAudio* dev) :
    audio_stream_out(),
    dev_(dev),
    device_(AUDIO_DEVICE_OUT_DEFAULT),
    frame_count_(0),
    left_volume_(0.0),
    right_volume_(0.0) { }

int GceAudioOutputStream::Dump(int fd) const {
  D("GceAudioOutputStream::%s", __FUNCTION__);
  VSOC_FDPRINTF(
      fd,
      "\tout_dump:\n"
      "\t\tsample rate: %u\n"
      "\t\tbuffer size: %zu\n"
      "\t\tchannel mask: %08x\n"
      "\t\tformat: %d\n"
      "\t\tdevice: %08x\n"
      "\t\taudio dev: %p\n\n",
      GetSampleRate(),
      GetBufferSize(),
      GetChannels(),
      GetFormat(),
      device_,
      dev_);
  return 0;
}

int GceAudioOutputStream::GetNextWriteTimestamp(int64_t* nstime) const {
  *nstime = cvd::time::Nanoseconds(
      buffer_->GetNextOutputBufferItemTime().SinceEpoch()).count();
  return 0;
}

namespace {
struct StrParmsDestroyer {
  void operator()(str_parms* parms) const {
    if (parms) {
      str_parms_destroy(parms);
    }
  }
};

typedef std::unique_ptr<str_parms, StrParmsDestroyer> StrParmsPtr;
}

int GceAudioOutputStream::SetParameters(const char* kv_pairs) {
  int err = 0;
  StrParmsPtr parms(str_parms_create_str(kv_pairs));
  {
    int fmt = 0;
    if (str_parms_get_int(parms.get(), AUDIO_PARAMETER_STREAM_FORMAT, &fmt)
        == 0) {
      SetFormat(static_cast<audio_format_t>(fmt));
    }
  }
  {
    int sample_rate = 0;
    if (str_parms_get_int(parms.get(), AUDIO_PARAMETER_STREAM_SAMPLING_RATE,
                          &sample_rate) == 0) {
      SetSampleRate(static_cast<uint32_t>(sample_rate));
    }
  }
  {
    int routing = 0;
    if (str_parms_get_int(parms.get(), AUDIO_PARAMETER_STREAM_ROUTING,
                          &routing) == 0) {
      device_ = static_cast<uint32_t>(routing);
    }
  }
  {
    int channels = 0;
    if (str_parms_get_int(parms.get(), AUDIO_PARAMETER_STREAM_CHANNELS,
                          &channels) == 0) {
      message_header_.channel_mask = static_cast<audio_channel_mask_t>(channels);
    }
  }
  {
    int frame_count = 0;
    if (str_parms_get_int(parms.get(), AUDIO_PARAMETER_STREAM_FRAME_COUNT,
                          &frame_count) == 0) {
      frame_count_ = static_cast<size_t>(frame_count);
    }
  }
  {
    int input_source = 0;
    if (str_parms_get_int(parms.get(), AUDIO_PARAMETER_STREAM_INPUT_SOURCE,
                          &input_source) == 0){
      ALOGE("GceAudioOutputStream::%s AUDIO_PARAMETER_STREAM_INPUT_SOURCE"
            " passed to an output stream", __FUNCTION__);
      err = -EINVAL;
    }
  }
  return err;
}

void GceAudioOutputStream::AddIntIfKeyPresent(
    /*const */ str_parms* query, str_parms* reply, const char* key, int value) {
  if (str_parms_get_str(query, key, NULL, 0) >= 0) {
    str_parms_add_int(reply, key, value);
  }
}


char* GceAudioOutputStream::GetParameters(const char* keys) const {
  D("GceAudioOutputStream::%s", __FUNCTION__);
  if (keys) D("%s keys %s", __FUNCTION__, keys);

  StrParmsPtr query(str_parms_create_str(keys));
  StrParmsPtr reply(str_parms_create());

  AddIntIfKeyPresent(query.get(), reply.get(),
                     AUDIO_PARAMETER_STREAM_FORMAT,
                     static_cast<int>(GetFormat()));
  AddIntIfKeyPresent(query.get(), reply.get(),
                     AUDIO_PARAMETER_STREAM_SAMPLING_RATE,
                     static_cast<int>(GetSampleRate()));
  AddIntIfKeyPresent(query.get(), reply.get(),
                     AUDIO_PARAMETER_STREAM_ROUTING,
                     static_cast<int>(device_));
  AddIntIfKeyPresent(query.get(), reply.get(),
                     AUDIO_PARAMETER_STREAM_CHANNELS,
                     static_cast<int>(message_header_.channel_mask));
  AddIntIfKeyPresent(query.get(), reply.get(),
                     AUDIO_PARAMETER_STREAM_FRAME_COUNT,
                     static_cast<int>(frame_count_));

  char *str = str_parms_to_str(reply.get());
  return str;
}

int GceAudioOutputStream::GetRenderPosition(uint32_t* dsp_frames) const {
  *dsp_frames = buffer_->GetCurrentItemNum();
  return 0;
}

ssize_t GceAudioOutputStream::Write(const void* buffer, size_t length) {
  // We're always the blocking case for now.
  static const bool blocking = true;
  message_header_.frame_size = frame_size_;
  frame_count_ += message_header_.num_frames_presented = length / frame_size_;
  message_header_.message_type = gce_audio_message::DATA_SAMPLES;
  // First do a nonblocking add
  int64_t frames_accepted_without_blocking = buffer_->AddToOutputBuffer(
      message_header_.num_frames_presented, false);
  // This seems backward, but adding the items to the buffer first
  // allows us to calculate the right frame number in the case of underflow.
  message_header_.frame_num =
      buffer_->GetNextOutputBufferItemNum() - frames_accepted_without_blocking;
  message_header_.time_presented =
      buffer_->GetLastUpdatedTime().SinceEpoch().GetTS();
  // We want to send the message before blocking. If we're in blocking mode
  // we will accept all of the frames.
  if (blocking) {
    message_header_.num_frames_accepted =
        message_header_.num_frames_presented;
  } else {
    message_header_.num_frames_accepted = frames_accepted_without_blocking;
  }
  // Never exceed the maximum packet size, as defined by the interface.
  // Clip off any frames that we can't transmit and increment the clipped
  // count.
  size_t transmitted_frame_size = length;
  if (length > gce_audio_message::kMaxAudioFrameLen) {
    transmitted_frame_size = gce_audio_message::kMaxAudioFrameLen;
    message_header_.num_packets_shortened++;
  }
  message_header_.total_size =
      sizeof(message_header_) + transmitted_frame_size;
  // Now send the message. Do not block if the receiver isn't ready
  // If this is a blocking write we will block after we have attempted to
  // send the data to the receiver.
  msghdr msg;
  iovec msg_iov[2];
  // We need a cast here because iov_base is defined non-const to support
  // recvmsg et.al.
  // There is no danger here:sendmsg does not write to the buffer.
  msg_iov[0].iov_base = &message_header_;
  msg_iov[0].iov_len = sizeof(message_header_);
  msg_iov[1].iov_base = const_cast<void*>(buffer);
  msg_iov[1].iov_len = transmitted_frame_size;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = msg_iov;
  msg.msg_iovlen = arraysize(msg_iov);
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
  if (dev_->SendMsg(msg, MSG_DONTWAIT) < 0) {
    message_header_.num_packets_dropped++;
  }
  if (!blocking) {
    return frames_accepted_without_blocking * frame_size_;
  }
  if ((message_header_.num_frames_presented) >
      static_cast<size_t>(frames_accepted_without_blocking)) {
    buffer_->AddToOutputBuffer(
        message_header_.num_frames_presented -
        frames_accepted_without_blocking, true);
  }
  return message_header_.num_frames_presented * frame_size_;
}

int GceAudioOutputStream::Open(
    GceAudio* dev, audio_io_handle_t /*handle*/,
    audio_devices_t devices, audio_output_flags_t /*flags*/,
    audio_config* config, uint32_t stream_number,
    GceAudioOutputStream** stream_out) {
  D("GceAudioOutputStream::%s", __FUNCTION__);
  *stream_out = NULL;
  // Deleted by Close(); UniquePtr holds until end of Open().
  std::unique_ptr<GceAudioOutputStream> out(
      new GceAudioOutputStream(dev));
  out->message_header_.stream_number = stream_number;
  out->message_header_.format = config->format;
  out->message_header_.channel_mask = config->channel_mask;
  out->message_header_.frame_rate = config->sample_rate;
  out->frame_count_ =
#if VSOC_PLATFORM_SDK_AFTER(K)
      config->frame_count;
#else
      0;
#endif
  out->common.get_sample_rate =
      Thunker<uint32_t()>::call<&GceAudioOutputStream::GetSampleRate>;
  out->common.set_sample_rate =
      Thunker<int(uint32_t)>::call<&GceAudioOutputStream::SetSampleRate>;
  out->common.get_buffer_size =
      Thunker<size_t()>::call<&GceAudioOutputStream::GetBufferSize>;
  out->common.get_channels =
      Thunker<audio_channel_mask_t()>::call<
        &GceAudioOutputStream::GetChannels>;
  out->common.get_format = Thunker<audio_format_t()>::call<
    &GceAudioOutputStream::GetFormat>;
  out->common.set_format = Thunker<int(audio_format_t)>::call<
    &GceAudioOutputStream::SetFormat>;
  out->common.standby = Thunker<int()>::call<&GceAudioOutputStream::Standby>;
  out->common.dump = Thunker<int(int)>::call<&GceAudioOutputStream::Dump>;
  out->common.get_device = Thunker<audio_devices_t()>::call<
    &GceAudioOutputStream::GetDevice>;
  out->common.set_device = Thunker<int(audio_devices_t)>::call<
    &GceAudioOutputStream::SetDevice>;
  out->common.set_parameters =
      Thunker<int(const char*)>::call<
      &GceAudioOutputStream::SetParameters>;
  out->common.get_parameters =
      Thunker<char*(const char *)>::call<
        &GceAudioOutputStream::GetParameters>;
  out->common.add_audio_effect =
      Thunker<int(effect_handle_t)>::call<
        &GceAudioOutputStream::AddAudioEffect>;
  out->common.remove_audio_effect =
      Thunker<int(effect_handle_t)>::call<
        &GceAudioOutputStream::RemoveAudioEffect>;
  out->get_latency =
      OutThunker<uint32_t()>::call<
        &GceAudioOutputStream::GetLatency>;
  out->set_volume =
      OutThunker<int(float, float)>::call<&GceAudioOutputStream::SetVolume>;
  out->write =
      OutThunker<ssize_t(const void*, size_t)>::call<
        &GceAudioOutputStream::Write>;
  out->get_render_position =
      OutThunker<int(uint32_t*)>::call<
        &GceAudioOutputStream::GetRenderPosition>;
  out->get_next_write_timestamp =
      OutThunker<int(int64_t*)>::call<
        &GceAudioOutputStream::GetNextWriteTimestamp>;
  out->device_ = devices;
  out->frame_size_ = GceAudioFrameSize(out.get());

  int64_t item_capacity =
      out->frame_size_  == 0 ? 0 : out->GetBufferSize() / out->frame_size_;
  if (item_capacity == 0) {
    ALOGE("Attempt to create GceAudioOutputStream with frame_size_ of 0.");
    return -EINVAL;
  }
  out->buffer_.reset(
      new SimulatedOutputBuffer(
          config->sample_rate, item_capacity));
  *stream_out = out.release();
  return 0;
}

}  // namespace cvd
