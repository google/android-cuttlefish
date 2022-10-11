/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/frontend/webrtc/libcommon/audio_device.h"

#include <string.h>

#include <android-base/logging.h>

#include <chrono>
#include <thread>

namespace cuttlefish {
namespace webrtc_streaming {

CfAudioDeviceModule::CfAudioDeviceModule() {}

int CfAudioDeviceModule::GetMoreAudioData(void* data, int bytes_per_sample,
                                          int samples_per_channel,
                                          int num_channels, int sample_rate,
                                          bool& muted) {
  muted = !playing_ || !audio_callback_;
  if (muted) {
    return 0;
  }

  size_t read_samples;
  int64_t elapsed_time;
  int64_t ntp_time_ms;
  auto res = audio_callback_->NeedMorePlayData(
      samples_per_channel, bytes_per_sample, num_channels, sample_rate, data,
      read_samples, &elapsed_time, &ntp_time_ms);
  if (res != 0) {
    return res;
  }
  return read_samples / num_channels;
}

// Retrieve the currently utilized audio layer
int32_t CfAudioDeviceModule::ActiveAudioLayer(AudioLayer* audioLayer) const {
  return -1;
}

// Full-duplex transportation of PCM audio
int32_t CfAudioDeviceModule::RegisterAudioCallback(
    webrtc::AudioTransport* audio_callback) {
  audio_callback_ = audio_callback;
  return 0;
}

// Main initialization and termination
int32_t CfAudioDeviceModule::Init() { return 0; }
int32_t CfAudioDeviceModule::Terminate() { return 0; }
bool CfAudioDeviceModule::Initialized() const { return true; }

// Device enumeration
int16_t CfAudioDeviceModule::PlayoutDevices() { return 1; }
int16_t CfAudioDeviceModule::RecordingDevices() { return 1; }
int32_t CfAudioDeviceModule::PlayoutDeviceName(
    uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
    char guid[webrtc::kAdmMaxGuidSize]) {
  if (index != 0) {
    return -1;
  }
  constexpr auto device_name = "Cuttlefish Webrtc Audio";
  constexpr auto device_guid = "Cuttlefish Webrtc Audio Device Id";
  strncpy(name, device_name, webrtc::kAdmMaxDeviceNameSize);
  name[webrtc::kAdmMaxDeviceNameSize - 1] = '\0';
  strncpy(guid, device_guid, webrtc::kAdmMaxGuidSize);
  guid[webrtc::kAdmMaxGuidSize - 1] = '\0';
  return 0;
}
int32_t CfAudioDeviceModule::RecordingDeviceName(
    uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
    char guid[webrtc::kAdmMaxGuidSize]) {
  if (index != 0) {
    return -1;
  }
  constexpr auto device_name = "Cuttlefish Webrtc Audio";
  constexpr auto device_guid = "Cuttlefish Webrtc Audio Device Id";
  strncpy(name, device_name, webrtc::kAdmMaxDeviceNameSize);
  name[webrtc::kAdmMaxDeviceNameSize - 1] = '\0';
  strncpy(guid, device_guid, webrtc::kAdmMaxGuidSize);
  guid[webrtc::kAdmMaxGuidSize - 1] = '\0';
  return 0;
}

// Device selection
int32_t CfAudioDeviceModule::SetPlayoutDevice(uint16_t index) { return 0; }
int32_t CfAudioDeviceModule::SetPlayoutDevice(WindowsDeviceType device) {
  return -1;
}
int32_t CfAudioDeviceModule::SetRecordingDevice(uint16_t index) { return 0; }
int32_t CfAudioDeviceModule::SetRecordingDevice(WindowsDeviceType device) {
  return -1;
}

// Audio transport initialization
int32_t CfAudioDeviceModule::PlayoutIsAvailable(bool* available) {
  *available = true;
  return 0;
}
int32_t CfAudioDeviceModule::InitPlayout() { return 0; }
bool CfAudioDeviceModule::PlayoutIsInitialized() const { return true; }
int32_t CfAudioDeviceModule::RecordingIsAvailable(bool* available) {
  *available = 0;
  return 0;
}
int32_t CfAudioDeviceModule::InitRecording() { return 0; }
bool CfAudioDeviceModule::RecordingIsInitialized() const { return true; }

// Audio transport control
int32_t CfAudioDeviceModule::StartPlayout() {
  playing_ = true;
  return 0;
}
int32_t CfAudioDeviceModule::StopPlayout() {
  playing_ = false;
  return 0;
}
bool CfAudioDeviceModule::Playing() const { return playing_; }
int32_t CfAudioDeviceModule::StartRecording() {
  recording_ = true;
  return 0;
}
int32_t CfAudioDeviceModule::StopRecording() {
  recording_ = false;
  return 0;
}
bool CfAudioDeviceModule::Recording() const { return recording_; }

// Audio mixer initialization
int32_t CfAudioDeviceModule::InitSpeaker() { return -1; }
bool CfAudioDeviceModule::SpeakerIsInitialized() const { return false; }
int32_t CfAudioDeviceModule::InitMicrophone() { return 0; }
bool CfAudioDeviceModule::MicrophoneIsInitialized() const { return true; }

// Speaker volume controls
int32_t CfAudioDeviceModule::SpeakerVolumeIsAvailable(bool* available) {
  *available = false;
  return 0;
}
int32_t CfAudioDeviceModule::SetSpeakerVolume(uint32_t volume) { return -1; }
int32_t CfAudioDeviceModule::SpeakerVolume(uint32_t* volume) const {
  return -1;
}
int32_t CfAudioDeviceModule::MaxSpeakerVolume(uint32_t* maxVolume) const {
  return -1;
}
int32_t CfAudioDeviceModule::MinSpeakerVolume(uint32_t* minVolume) const {
  return -1;
}

// Microphone volume controls
int32_t CfAudioDeviceModule::MicrophoneVolumeIsAvailable(bool* available) {
  *available = false;
  return 0;
}
int32_t CfAudioDeviceModule::SetMicrophoneVolume(uint32_t volume) { return -1; }
int32_t CfAudioDeviceModule::MicrophoneVolume(uint32_t* volume) const {
  return -1;
}
int32_t CfAudioDeviceModule::MaxMicrophoneVolume(uint32_t* maxVolume) const {
  return -1;
}
int32_t CfAudioDeviceModule::MinMicrophoneVolume(uint32_t* minVolume) const {
  return -1;
}

// Speaker mute control
int32_t CfAudioDeviceModule::SpeakerMuteIsAvailable(bool* available) {
  *available = false;
  return 0;
}
int32_t CfAudioDeviceModule::SetSpeakerMute(bool enable) { return -1; }
int32_t CfAudioDeviceModule::SpeakerMute(bool* enabled) const { return -1; }

// Microphone mute control
int32_t CfAudioDeviceModule::MicrophoneMuteIsAvailable(bool* available) {
  *available = false;
  return 0;
}
int32_t CfAudioDeviceModule::SetMicrophoneMute(bool enable) { return -1; }
int32_t CfAudioDeviceModule::MicrophoneMute(bool* enabled) const { return -1; }

// Stereo support
int32_t CfAudioDeviceModule::StereoPlayoutIsAvailable(bool* available) const {
  *available = true;
  return 0;
}
int32_t CfAudioDeviceModule::SetStereoPlayout(bool enable) {
  stereo_playout_enabled_ = enable;
  return 0;
}
int32_t CfAudioDeviceModule::StereoPlayout(bool* enabled) const {
  *enabled = stereo_playout_enabled_;
  return 0;
}
int32_t CfAudioDeviceModule::StereoRecordingIsAvailable(bool* available) const {
  *available = true;
  return 0;
}
int32_t CfAudioDeviceModule::SetStereoRecording(bool enable) {
  stereo_recording_enabled_ = enable;
  return 0;
}
int32_t CfAudioDeviceModule::StereoRecording(bool* enabled) const {
  *enabled = stereo_recording_enabled_;
  return 0;
}

// Playout delay
int32_t CfAudioDeviceModule::PlayoutDelay(uint16_t* delayMS) const {
  // There is currently no way to estimate the real delay for thiese streams.
  // Given that 10ms buffers are used almost everywhere in the pipeline we know
  // the delay is at least 10ms, so that's the best guess here.
  *delayMS = 10;
  return 0;
}

// Only supported on Android.
bool CfAudioDeviceModule::BuiltInAECIsAvailable() const { return false; }
bool CfAudioDeviceModule::BuiltInAGCIsAvailable() const { return false; }
bool CfAudioDeviceModule::BuiltInNSIsAvailable() const { return false; }

// Enables the built-in audio effects. Only supported on Android.
int32_t CfAudioDeviceModule::EnableBuiltInAEC(bool enable) { return -1; }
int32_t CfAudioDeviceModule::EnableBuiltInAGC(bool enable) { return -1; }
int32_t CfAudioDeviceModule::EnableBuiltInNS(bool enable) { return -1; }

int32_t CfAudioDeviceModule::GetPlayoutUnderrunCount() const { return -1; }

}  // namespace webrtc_streaming
}  // namespace cuttlefish
