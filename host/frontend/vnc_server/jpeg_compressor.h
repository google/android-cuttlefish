#ifndef DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_JPEG_COMPRESSOR_H_
#define DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_JPEG_COMPRESSOR_H_

#include "vnc_utils.h"

#include <cstdint>
#include <cstdlib>
#include <memory>

namespace avd {
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
}  // namespace avd

#endif
