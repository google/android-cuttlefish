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

#pragma once

#include <stddef.h>

namespace cuttlefish {

namespace webrtc_streaming {

// Interface to provide access to a stream originated on the client.
class AudioSource {
 public:
  // Returns the number of bytes read or a negative number in case of errors. If
  // muted is set to true, the contents of data should be considered to be all
  // 0s.
  virtual int GetMoreAudioData(void* data, int bytes_per_sample,
                               int samples_per_channel, int num_channels,
                               int sample_rate, bool& muted) = 0;

 protected:
  virtual ~AudioSource() = default;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
