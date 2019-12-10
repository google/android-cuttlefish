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
#include <SDL2/SDL.h>
#include <cstdint>

namespace cfp {

class SDLLib;

class SDLAudioDevice {
 public:
  SDLAudioDevice(SDLAudioDevice&& other);
  SDLAudioDevice& operator=(SDLAudioDevice&& other);

  SDLAudioDevice(const SDLAudioDevice&) = delete;
  SDLAudioDevice& operator=(const SDLAudioDevice&) = delete;

  ~SDLAudioDevice();

  int QueueAudio(const void* data, std::uint32_t len);

 private:
  friend SDLLib;
  explicit SDLAudioDevice(SDL_AudioDeviceID device_id);
  void close();

  SDL_AudioDeviceID device_id_{};
};

class SDLLib {
 public:
  SDLLib();
  ~SDLLib();

  SDLLib(const SDLLib&) = delete;
  SDLLib& operator=(const SDLLib&) = delete;

  SDLAudioDevice OpenAudioDevice(int freq, std::uint8_t num_channels);
};

}  // namespace cfp
