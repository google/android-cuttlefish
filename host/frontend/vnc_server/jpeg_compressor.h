#pragma once

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

#include <cstdint>
#include <cstdlib>
#include <memory>

#include "host/frontend/vnc_server/vnc_utils.h"

namespace cuttlefish {
namespace vnc {

// libjpeg-turbo with jpeg_mem_dest (using memory as a destination) is funky.
// If you give it a buffer that is big enough it will use it.
// If you give it a buffer that is too small, it will allocate a new buffer
// but will NOT free the buffer you gave it.
// This class keeps track of the capacity of the working buffer, and frees the
// old buffer if libjpeg-turbo silently discards it.
class JpegCompressor {
 public:
  Message Compress(const Message& frame, int jpeg_quality, std::uint16_t x,
                   std::uint16_t y, std::uint16_t width, std::uint16_t height,
                   int screen_width);

 private:
  void UpdateBuffer(std::uint8_t* compression_buffer,
                    unsigned long compression_buffer_size);
  struct Freer {
    void operator()(void* p) const { std::free(p); }
  };

  std::unique_ptr<std::uint8_t, Freer> buffer_;
  unsigned long buffer_capacity_{};
};

}  // namespace vnc
}  // namespace cuttlefish
