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

#include <list>
#include <map>
#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/threads/cuttlefish_thread.h"
#include "common/vsoc/lib/audio_data_region_view.h"
#include "guest/hals/audio/audio_hal.h"
#include "guest/hals/audio/vsoc_audio_input_stream.h"
#include "guest/hals/audio/vsoc_audio_message.h"
#include "guest/libs/platform_support/api_level_fixes.h"

namespace cvd {

class GceAudioInputStream;
class GceAudioOutputStream;

class GceAudio : public audio_hw_device {
 public:
  // This common code manipulates the parameters of input and output streams.
  static int SetStreamParameters(struct audio_stream *, const char *);

  ~GceAudio();

  // Non-HAL methods that are part of the GCE implementation.
  // Most of these are used by the input and output streams.

  // Returns true if the microphone is muted. Used by input streams.
  bool IsMicrophoneMuted() {
    cvd::LockGuard<cvd::Mutex> guard(lock_);
    return mic_muted_;
  }

  // Send a message to the connected streamer.
  // Returns:
  //   0 if there is no streamer.
  //   >0 if the message was sent.
  //   -1 if there was an error.
  ssize_t SendMsg(const msghdr&, int flags);

  // Sends a stream update to the connected streamer.
  // Stream updates have no frames. Use SendMsg if the message has frames.
  //   0 if there is no streamer.
  //   >0 if the message was sent.
  //   -1 if there was an error.
  ssize_t SendStreamUpdate(
      const gce_audio_message& stream_info, int flags);

  // Callbacks for the Android audio_module HAL interface.
  // Most of the comments below are copied from
  // libhardware/include/hardware/audio.h
  //
  // Where the is a conflict the comments there apply.
  // By default these methods return 0 on success -<errno> for failure.

  // Opens the device.
  static int Open(const hw_module_t* module, const char* name,
                  hw_device_t** device);

  // Closes the device, closing any open input streams and output streams.
  int Close();

  // Closes the input stream, throwing away any data in the buffer.
  void CloseInputStream(audio_stream_in* stream);

  // Closes the output stream without waiting for the buffer to clear.
  void CloseOutputStream(audio_stream_out* stream);

  // Creates an audio patch between several source and sink ports.
  // The handle is allocated by the HAL and should be unique for this
  // audio HAL module.
  // TODO(ghartman): Implement this as part of HAL 3.0
  //int CreateAudioPatch(unsigned int num_sources,
  //                     const struct audio_port_config *sources,
  //                     unsigned int num_sinks,
  //                     const struct audio_port_config *sinks,
  //                     audio_patch_handle_t *handle);

  // dumps the state of the audio hardware to the given fd.
  // This information can be retrieved using the dumpsys utility.
  int Dump(int fd) const;

  // Fills the list of supported attributes for a given audio port.
  // As input, "port" contains the information (type, role, address etc...)
  // needed by the HAL to identify the port.
  // As output, "port" contains possible attributes (sampling rates, formats,
  // channel masks, gain controllers...) for this port.
  // TODO(ghartman): Implement this as part of HAL 3.0
  // int GetAudioPort(struct audio_port *port);

  // Sets audio port configuration
  // TODO(ghartman): Implement this as part of HAL 3.0
  // int SetAudioPortConfig(const struct audio_port_config *config);

  size_t GetInputBufferSize(const audio_config*) const;

  // Gets the current master volume value for the HAL, if the HAL supports
  // master volume control.  AudioFlinger will query this value from the
  // primary audio HAL when the service starts and use the value for setting
  // the initial master volume across all HALs.  HALs which do not support
  // this method may leave it set to NULL.
  int GetMasterVolume(float* /*volume*/);

  // Get the current master mute status for the HAL, if the HAL supports
  // master mute control.  AudioFlinger will query this value from the primary
  // audio HAL when the service starts and use the value for setting the
  // initial master mute across all HALs.  HALs which do not support this
  // method may leave it set to NULL.
  int GetMasterMute(bool* muted);

  // Gets the audio mute status for the microphone.
  int GetMicMute(bool* state) const;

  // Retrieves the global audio parameters.
  // TODO(ghartman): Implement this.
  char* GetParameters(const char* keys) const;

  // Enumerates what devices are supported by each audio_hw_device
  // implementation.
  // Return value is a bitmask of 1 or more values of audio_devices_t
  // used by audio flinger.
  // NOTE: audio HAL implementations starting with
  // AUDIO_DEVICE_API_VERSION_2_0 do not implement this function.
  // AUDIO_DEVICE_API_VERSION_2_0 was the current version as of JB-MR1
  // All supported devices should be listed in audio_policy.conf
  // file and the audio policy manager must choose the appropriate
  // audio module based on information in this file.
  uint32_t GetSupportedDevices() const;

  // Checks to see if the audio hardware interface has been initialized.
  // Always returns 0 to indicate success, but -ENODEV is also allowed to
  // indicate failure.
  int InitCheck() const;

  // Creates an additional hardware input stream.
  // Additional parameters were added in the 3.0 version of the HAL.
  // These defaults make it easier to implement a cross-branch device.
  int OpenInputStream(
      audio_io_handle_t handle,
      audio_devices_t devices, audio_config *config,
      audio_stream_in **stream_in,
      audio_input_flags_t flags = AUDIO_INPUT_FLAG_NONE,
      const char* address = 0,
      audio_source_t source = AUDIO_SOURCE_DEFAULT);

  // Creates an additional output stream.
  // The "address" parameter qualifies the "devices" audio device type if
  // needed. On GCE we ignore it for now because we simulate a single SoC
  // hw devices.
  //
  // The format format depends on the device type:
  //   Bluetooth devices use the MAC address of the device in the form
  //     "00:11:22:AA:BB:CC"
  // USB devices use the ALSA card and device numbers in the form
  //     "card=X;device=Y"
  // Other devices may use a number or any other string.
  int OpenOutputStream(
      audio_io_handle_t handle,
      audio_devices_t devices, audio_output_flags_t flags,
      audio_config* config, audio_stream_out** stream_out,
      const char* address = 0);

  // Releases an audio patch.
  // TODO(ghartman): Implement this as part of HAL 3.0
  //int ReleaseAudioPatch(audio_patch_handle_t handle);

  // Sets the audio mute status for all audio activities.  If any value other
  // than 0 is returned, the software mixer will emulate this capability.
  // The GCE implementation always returns 0.
  int SetMasterMute(bool muted);

  // Sets the audio volume for all audio activities other than voice call.
  // Range between 0.0 and 1.0. If any value other than 0 is returned,
  // the software mixer will emulate this capability.
  // The GCE implementation always returns 0.
  int SetMasterVolume(float volume);

  // Sets the audio mute status for the microphone.
  int SetMicMute(bool state);

  // set_mode is called when the audio mode changes. AUDIO_MODE_NORMAL mode
  // is for standard audio playback, AUDIO_MODE_RINGTONE when a ringtone is
  // playing, and AUDIO_MODE_IN_CALL when a call is in progress.
  int SetMode(audio_mode_t mode);

  // Sets the global audio parameters.
  // TODO(ghartman): Create a sensible implementation.
  int SetParameters(const char* kvpairs);

  // Sets the audio volume of a voice call. Range is between 0.0 and 1.0
  int SetVoiceVolume(float volume);


 private:
  // HAL 3.0 modifies the signatures of OpenInputStream and OpenOutputStream.
  // We don't want to fork the implementation, and we don't want #ifdefs all
  // over the code. The current implementation defines OpenInputStream and
  // OpenOutputStream with default values for the paramteres that were added,
  // and then generates a HAL-specific wrapper to be used in the function
  // table.
#if defined(AUDIO_DEVICE_API_VERSION_3_0)
  typedef int OpenInputStreamHAL_t(
      audio_io_handle_t, audio_devices_t, audio_config*, audio_stream_in**,
      audio_input_flags_t, const char*, audio_source_t);

  int OpenInputStreamCurrentHAL(
      audio_io_handle_t a, audio_devices_t b, audio_config* c,
      audio_stream_in** d, audio_input_flags_t e, const char* f,
      audio_source_t g) {
    return OpenInputStream(a, b, c, d, e, f, g);
  }

  typedef int OpenOutputStreamHAL_t(
      audio_io_handle_t, audio_devices_t, audio_output_flags_t,
      audio_config*, audio_stream_out**,
      const char*);

  int OpenOutputStreamCurrentHAL(
      audio_io_handle_t a, audio_devices_t b, audio_output_flags_t c,
      audio_config* d, audio_stream_out** e,
      const char* f) {
    return OpenOutputStream(a, b, c, d, e, f);
  }
#else
  typedef int OpenInputStreamHAL_t(
      audio_io_handle_t, audio_devices_t, audio_config*, audio_stream_in**);

  int OpenInputStreamCurrentHAL(
      audio_io_handle_t a, audio_devices_t b, audio_config* c,
      audio_stream_in** d) {
    return OpenInputStream(a, b, c, d);
  }

  typedef int OpenOutputStreamHAL_t(
      audio_io_handle_t, audio_devices_t, audio_output_flags_t,
      audio_config*, audio_stream_out**);

  int OpenOutputStreamCurrentHAL(
      audio_io_handle_t a, audio_devices_t b, audio_output_flags_t c,
      audio_config* d, audio_stream_out** e) {
    return OpenOutputStream(a, b, c, d, e);
  }
#endif

  //TODO(ghartman): Update this when we support 3.0.
#if defined(AUDIO_DEVICE_API_VERSION_2_0)
  static const unsigned int version_ = AUDIO_DEVICE_API_VERSION_2_0;
#else
  static const unsigned int version_ = AUDIO_DEVICE_API_VERSION_1_0;
#endif

  using AudioDataRegionView = vsoc::audio_data::AudioDataRegionView;
  std::shared_ptr<AudioDataRegionView> audio_data_rv_;
  std::unique_ptr<vsoc::RegionWorker> audio_worker_;

  // Lock to protect the data below.
  mutable cvd::Mutex lock_;
  // State that is managed at the device level.
  float voice_volume_;
  float master_volume_;
  bool master_muted_;
  bool mic_muted_;
  audio_mode_t mode_;
  // There can be multiple input and output streams. This field is used
  // to assign each one a unique identifier.
  // TODO(ghartman): This can wrap after 2^32 streams. Ideally we should check
  // the output_list_ to ensure that the stream number hasn't been assigned.
  // However, streams don't really appear and disapper that often.
  // We use the same counter for both input and output streams to make things
  // a little easier on the client.
  uint32_t next_stream_number_;
  // List of the currently active output streams.
  // Used to clean things up Close()
  std::list<GceAudioOutputStream *> output_list_;
  // List of the currently active input streams.
  // Used to clean things up Close()
  typedef std::map<uint32_t, GceAudioInputStream *> input_map_t;
  input_map_t input_map_;

  GceAudio() :
      audio_hw_device(),
      voice_volume_(0.0),
      master_volume_(0.0),
      master_muted_(false),
      mic_muted_(false),
      mode_(AUDIO_MODE_NORMAL),
      next_stream_number_(1) { }
};

}
