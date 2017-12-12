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
#pragma once

#include <memory>

#include "guest/commands/audio/audio_hal.h"
#include "guest/commands/audio/simulated_buffer.h"
#include "guest/commands/audio/vsoc_audio_message.h"

namespace cvd {

// Defines static callback functions for the audio_stream and audio_stream_out
// interfaces in  libhardware/include/hardware/audio.h
//
// Where the is a conflict the comments there apply.
// By default these methods return 0 on success -<errno> for failure.
class GceAudioOutputStream : public audio_stream_out {
 public:
  // Factory method for a new output stream.
  static int Open(GceAudio* dev, audio_io_handle_t handle,
                  audio_devices_t devices, audio_output_flags_t flags,
                  audio_config* config, uint32_t stream_number,
                  GceAudioOutputStream** stream_out);

  gce_audio_message GetStreamDescriptor(
      gce_audio_message::message_t message_type) const {
    gce_audio_message rval = message_header_;
    rval.total_size = sizeof(rval);
    rval.header_size = sizeof(rval);
    rval.message_type = message_type;
    rval.num_frames_presented = 0;
    rval.num_frames_accepted = 0;
    return rval;
  }

  // Method from audio_stream, listed in order of appearance.
  // TODO(ghartman): Consider moving these if they could be shared with
  // gce_audio_input_stream.


  // Returns the sampling rate in Hz - eg. 44100.
  uint32_t GetSampleRate() const {
    return message_header_.frame_rate;
  }

  // Sets the sample rate
  //   AUDIO_PARAMETER_STREAM_SAMPLING_RATE
  int SetSampleRate(uint32_t sample_rate) {
    if (sample_rate != message_header_.frame_rate) {
      message_header_.frame_rate = sample_rate;
      // TODO(ghartman): The output buffer should be quantized at about 192
      // bytes for better fidelity. Do this by passing
      // frame_rate * frame_size / 192 and then rescaling the outputs.
      // Or we could always create a quantized wrapper of the buffer...
      buffer_.reset(
          new SimulatedOutputBuffer(
              sample_rate, GetBufferSize() / frame_size_));
    }
    return 0;
  }

  // Returns the size of input/output buffer in bytes for this stream.
  // eg. 4800.
  // It should be a multiple of the frame size.  See also GetInputBufferSize.
  size_t GetBufferSize() const {
    return kOutBufferSize;
  }

  // Returns the channel mask -
  //  e.g. AUDIO_CHANNEL_OUT_STEREO or AUDIO_CHANNEL_IN_STEREO
  audio_channel_mask_t GetChannels() const {
    return message_header_.channel_mask;
  }

  // Returns the audio format - e.g. AUDIO_FORMAT_PCM_16_BIT
  audio_format_t GetFormat() const {
    return message_header_.format;
  }

  // Sets the audio format.
  // Unused as of JB - use set_parameters with key
  //   AUDIO_PARAMETER_STREAM_FORMAT
  int SetFormat(audio_format_t format) {
    message_header_.format = format;
    return 0;
  }

  // Puts the audio hardware input/output into standby mode.
  // Driver should exit from standby mode at the next I/O operation.
  // Returns 0 on success and <0 on failure.
  // TODO(ghartman): This should reset some of the frame counts.
  int Standby() {
    return 0;
  }

  // dumps the state of the audio hardware to the given fd.
  // This information can be retrieved using the dumpsys utility.
  int Dump(int fd) const;

  // Returns the set of device(s) which this stream is connected to.
  // TODO(ghartman): Implement this.
  audio_devices_t GetDevice() const { return device_; }

  // Not directly called from JB forward.
  // Called indirectly from SetParameters with the key
  //   AUDIO_PARAMETER_STREAM_ROUTING
  int SetDevice(audio_devices_t device) { device_ = device; return 0; }

  // Sets audio stream parameters. The function accepts a list of
  // parameter key value pairs in the form: key1=value1;key2=value2;...
  //
  // Some keys are reserved for standard parameters (See AudioParameter class)
  //
  // If the implementation does not accept a parameter change while
  // the output is active but the parameter is acceptable otherwise, it must
  // return -ENOSYS.
  //
  // The audio flinger will put the stream in standby and then change the
  // parameter value.
  int SetParameters(const char* kv_pairs);

  // Gets audio stream parameters. The function accepts a list of
  // keys in the form: key1=value1;key2=value2;...
  //
  // Returns a pointer to a heap allocated string. The caller is responsible
  // for freeing the memory for it using free().
  // TODO(ghartman): Implement this.
  char* GetParameters(const char* keys) const;

  // TODO(ghartman): Implement this.
  int AddAudioEffect(effect_handle_t /*effect*/) const {
    static unsigned int printed = 0;  // printed every 2^32-th call.
    ALOGE_IF(!printed++, "%s: not implemented", __FUNCTION__);
    return 0;
  }

  // TODO(ghartman): Implement this.
  int RemoveAudioEffect(effect_handle_t /*effect*/) const {
    static unsigned int printed = 0;  // printed every 2^32-th call.
    ALOGE_IF(!printed++, "%s: not implemented", __FUNCTION__);
    return 0;
  }

  // Methods defined in audio_stream_out

  // Returns the audio hardware driver estimated latency in milliseconds.
  // TODO(ghartman): Calculate this based on the format and the quantum.
  uint32_t GetLatency() const {
    return kOutLatency;
  }

  // Use this method in situations where audio mixing is done in the
  // hardware. This method serves as a direct interface with hardware,
  // allowing you to directly set the volume as apposed to via the framework.
  // This method might produce multiple PCM outputs or hardware accelerated
  // codecs, such as MP3 or AAC.
  //
  // Note that GCE simulates hardware mixing.
  int SetVolume(float left_volume, float right_volume) {
    left_volume_ = left_volume;
    right_volume_ = right_volume;
    return 0;
  }

  // Write audio buffer to driver. Returns number of bytes written, or a
  // negative android::status_t. If at least one frame was written successfully prior
  // to the error the driver will return that successful (short) byte count
  // and then return an error in the subsequent call.
  //
  // If SetCallback() has previously been called to enable non-blocking mode
  // the Write() is not allowed to block. It must write only the number of
  // bytes that currently fit in the driver/hardware buffer and then return
  // this byte count. If this is less than the requested write size the
  // callback function must be called when more space is available in the
  // driver/hardware buffer.
  ssize_t Write(const void* buffer, size_t bytes);

  // Returns the number of audio frames written by the audio dsp to DAC since
  // the output has exited standby
  // TODO(ghartman): Implement zeroing this in Standby().
  int GetRenderPosition(uint32_t* dsp_frames) const;

  // Gets the local time at which the next write to the audio driver will be
  // presented. The units are microseconds, where the epoch is decided by the
  // local audio HAL.
  //
  // The GCE implementation uses CLOCK_MONOTONIC, which also happens to line
  // up with LocalTime.
  int GetNextWriteTimestamp(int64_t*) const;

  // Turns on non-blocking mode and sets the callback function for notifying
  // completion of non-blocking write and drain.
  // Calling this function implies that all future Write() and Drain()
  // must be non-blocking and use the callback to signal completion.
  //
  // TODO(ghartman): Implement this URGENTLY.
  //
  // int SetCallback(stream_callback_t callback, void *cookie);

  // Notifies to the audio driver to stop playback however the queued buffers
  // are retained by the hardware. Useful for implementing pause/resume. Empty
  // implementation if not supported however should be implemented for hardware
  // with non-trivial latency. In the pause state audio hardware could still be
  // using power. User may consider calling suspend after a timeout.
  //
  // Implementation of this function is mandatory for offloaded playback.
  //
  // TODO(ghartman): Implement this URGENTLY. There is already support in
  // SimulatedBuffer.
  // int Pause();

  // Notifies to the audio driver to resume playback following a pause.
  // Returns error if called without matching pause.
  //
  // Implementation of this function is mandatory for offloaded playback.
  //
  // TODO(ghartman): Implement this URGENTLY.
  //
  // int Resume();

  // Requests notification when data buffered by the driver/hardware has
  // been played. If set_callback() has previously been called to enable
  // non-blocking mode, the drain() must not block, instead it should return
  // quickly and completion of the drain is notified through the callback.
  // If set_callback() has not been called, the drain() must block until
  // completion.
  //
  // If type==AUDIO_DRAIN_ALL, the drain completes when all previously written
  // data has been played.
  //
  // If type==AUDIO_DRAIN_EARLY_NOTIFY, the drain completes shortly before all
  // data for the current track has played to allow time for the framework
  // to perform a gapless track switch.
  //
  // Drain must return immediately on stop() and flush() call
  //
  // Implementation of this function is mandatory for offloaded playback.
  //
  // TODO(ghartman): Implement this URGENTLY.
  //
  // int Drain(audio_drain_type_t type);

  // Notifies to the audio driver to flush the queued data. Stream must already
  // be paused before calling Flush().
  //
  // Implementation of this function is mandatory for offloaded playback.
  //
  // TODO(ghartman): Implement this URGENTLY.
  //
  // int Flush();

  // Returns a recent count of the number of audio frames presented to an
  // external observer.  This excludes frames which have been written but are
  // still in the pipeline.
  //
  // The count is not reset to zero when output enters standby.
  // Also returns the value of CLOCK_MONOTONIC as of this presentation count.
  // The returned count is expected to be 'recent',
  // but does not need to be the most recent possible value.
  // However, the associated time should correspond to whatever count is
  // returned.
  //
  // Example:  assume that N+M frames have been presented, where M is a
  // 'small' number.
  // Then it is permissible to return N instead of N+M,
  // and the timestamp should correspond to N rather than N+M.
  // The terms 'recent' and 'small' are not defined.
  // They reflect the quality of the implementation.
  //
  // 3.0 and higher only.
  //
  // TODO(ghartman): Implement this URGENTLY.
  //
  // int GetPresentationPosition(uint64_t *frames, struct timespec *timestamp);

 private:
  // If key is present in query, add key=value; to reply.
  // query should be pointer to const, but the str_parms functions aren't
  // const-correct, so neither is this.
  static void AddIntIfKeyPresent(
      /*const*/ str_parms* query, str_parms* reply, const char* key, int value);


  explicit GceAudioOutputStream(cvd::GceAudio*);

  static const size_t kOutBufferSize = 3840;
  static const size_t kOutLatency = 2;

  gce_audio_message message_header_;
  std::unique_ptr<SimulatedOutputBuffer> buffer_;
  cvd::GceAudio *dev_;
  audio_devices_t device_;
  size_t frame_size_;
  size_t frame_count_;
  float left_volume_;
  float right_volume_;
};

}
