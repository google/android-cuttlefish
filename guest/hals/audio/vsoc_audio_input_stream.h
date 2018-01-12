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

#include <memory>

#include "guest/hals/audio/audio_hal.h"
#include "guest/hals/audio/simulated_buffer.h"
#include "guest/hals/audio/vsoc_audio_message.h"

namespace cvd {

namespace {
static const int IN_BUFFER_BYTES = 4096;
}

class GceAudio;

// Defines static callback functions for generic_stream_in HAL interface.
class GceAudioInputStream : public audio_stream_in {
 public:
  // These methods are internal to the GCE audio implementation.
  // Factory for new input streams.
  static int Open(
      GceAudio* dev, audio_io_handle_t handle,
      audio_devices_t devices, const audio_config& config,
      GceAudioInputStream** stream_in);

  // Gets a description of this stream
  gce_audio_message GetStreamDescriptor(
      uint32_t stream_number, gce_audio_message::message_t event);

  // audio_stream_in implementation. These definitions follow the ones
  // in hardware/libhardware/include/hardware/audio.h

  // Returns the sampling rate in Hz - eg. 44100.
  uint32_t GetSampleRate() const { return config_.sample_rate; }

  // Sets the sample rate
  // no direct calls from JB and later, but called indirectly from
  // GceAudio::SetStreamParamters when it finds
  // AUDIO_PARAMETER_STREAM_SAMPLING_RATE
  int SetSampleRate(uint32_t rate);

  // Returns the size of input/output buffer in bytes for this stream - eg.
  // 4800.
  // It should be a multiple of the frame size.  See also get_input_buffer_size
  size_t GetBufferSize() const {
    return IN_BUFFER_BYTES;
  }

  // Returns the channel mask -
  //   e.g. AUDIO_CHANNEL_OUT_STEREO or AUDIO_CHANNEL_IN_STEREO
  audio_channel_mask_t GetChannels() const {
    return config_.channel_mask;
  }

  // Returns the audio format - e.g. AUDIO_FORMAT_PCM_16_BIT
  audio_format_t GetFormat() const {
    return config_.format;
  }

  // Sets the audio format
  // no direct calls from JB and later, but called indirectly from
  // GceAudio::SetStreamParamters when it finds
  //   AUDIO_PARAMETER_STREAM_FORMAT
  int SetFormat(audio_format_t format);

  // Puts the audio hardware input/output into standby mode.
  // Driver should exit from standby mode at the next I/O operation.
  // Returns 0 on success and <0 on failure.
  int Standby() { return 0; }

  // Dumps the state of the audio input/output device
  int Dump(int fd) const;

  // Returns the set of device(s) which this stream is connected to
  audio_devices_t GetDevice() const {
    return device_;
  }

  // Sets the device this stream is connected to.
  // no direct calls from JB and later, but called indirectly from
  // GceAudio::SetStreamParamters when it finds
  //   AUDIO_PARAMETER_STREAM_ROUTING for both input and output.
  //   AUDIO_PARAMETER_STREAM_INPUT_SOURCE is an additional information used by
  //                                       input streams only.
  int SetDevice(audio_devices_t device) { device_ = device; return 0; }

  // sets audio stream parameters. The function accepts a list of
  // parameter key value pairs in the form: key1=value1;key2=value2;...
  //
  // Some keys are reserved for standard parameters (See AudioParameter class)
  //
  // If the implementation does not accept a parameter change while
  // the output is active but the parameter is acceptable otherwise, it must
  // return -ENOSYS.
  // The audio flinger will put the stream in standby and then change the
  // parameter value.
  // Uses GceAudio::SetStreamParameters

  // Returns a pointer to a heap allocated string. The caller is responsible
  // for freeing the memory for it using free().
  char* GetParameters(const char* keys) const;

  int AddAudioEffect(effect_handle_t /*effect*/) const {
    return 0;
  }

  int RemoveAudioEffect(effect_handle_t /*effect*/) const {
    return 0;
  }

  // Input stream specific methods

  // Sets the input gain for the audio driver. This method is for
  // for future use as of M.
  int SetGain(float gain) {
    gain_ = gain;
    return 0;
  }

  // Reads audio buffer in from audio driver. Returns number of bytes read, or
  // a negative android::status_t. If at least one frame was read prior to the error,
  //  read should return that byte count and then return an error in the
  // subsequent call.
  ssize_t Read(void* buffer, size_t bytes);

  // Return the amount of input frames lost in the audio driver since the
  // last call of this function.
  // Audio driver is expected to reset the value to 0 and restart counting
  // upon returning the current value by this function call.
  // Such loss typically occurs when the user space process is blocked
  // longer than the capacity of audio driver buffers.
  //
  // Unit: the number of input audio frames
  uint32_t GetInputFramesLost() {
    int64_t cur_lost_frames = buffer_model_->GetLostInputItems();
    uint32_t rval = cur_lost_frames - reported_lost_frames_;
    reported_lost_frames_ = cur_lost_frames;
    return rval;
  }

 private:
  GceAudioInputStream(cvd::GceAudio* dev, audio_devices_t devices,
                      const audio_config& config);
  std::unique_ptr<SimulatedInputBuffer> buffer_model_;
  cvd::GceAudio *dev_;
  audio_config config_;
  float gain_;
  audio_devices_t device_;
  size_t frame_size_;
  int64_t reported_lost_frames_;
};

}
