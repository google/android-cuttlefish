/*
 *
 * Copyright (C) 2018 The Android Open Source Project
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

#include "sdl_wrapper.h"

#include "android-base/logging.h"
#include <SDL2/SDL.h>

#include <cstdint>

using cfp::SDLAudioDevice;
using cfp::SDLLib;

SDLAudioDevice::SDLAudioDevice(SDLAudioDevice&& other)
    : device_id_{other.device_id_} {
  other.device_id_ = 0;
}
SDLAudioDevice& SDLAudioDevice::operator=(SDLAudioDevice&& other) {
  close();
  device_id_ = other.device_id_;
  other.device_id_ = 0;
  return *this;
}

SDLAudioDevice::~SDLAudioDevice() { close(); }

int SDLAudioDevice::QueueAudio(const void* data, std::uint32_t len) {
  return SDL_QueueAudio(device_id_, data, len);
}

SDLAudioDevice::SDLAudioDevice(SDL_AudioDeviceID device_id)
    : device_id_{device_id} {}

void SDLAudioDevice::close() {
  if (device_id_ != 0) {
    SDL_CloseAudioDevice(device_id_);
  }
}

SDLLib::SDLLib() { SDL_Init(SDL_INIT_AUDIO); }
SDLLib::~SDLLib() { SDL_Quit(); }

SDLAudioDevice SDLLib::OpenAudioDevice(int freq, std::uint8_t num_channels) {
  SDL_AudioSpec wav_spec{};
  wav_spec.freq = freq;
  wav_spec.format = AUDIO_S16LSB;
  wav_spec.channels = num_channels;
  wav_spec.silence = 0;
  // .samples seems to work as low as 256,
  // docs say this is 4096 when used with SDL_LoadWAV so I'm sticking with
  // that
  wav_spec.samples = 4096;
  wav_spec.size = 0;

  auto audio_device_id = SDL_OpenAudioDevice(nullptr, 0, &wav_spec, nullptr, 0);
  if (audio_device_id == 0) {
    LOG(FATAL) << "failed to open audio device: " << SDL_GetError() << '\n';
  }
  SDL_PauseAudioDevice(audio_device_id, false);
  return SDLAudioDevice{audio_device_id};
}
