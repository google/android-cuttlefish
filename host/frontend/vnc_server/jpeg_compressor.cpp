#include "jpeg_compressor.h"
#include "vnc_utils.h"

#include <jpeglib.h>
#include <stdio.h>  // stdio.h must appear before jpeglib.h

#define LOG_TAG "GceVNCServer"
#include <cutils/log.h>

using avd::vnc::JpegCompressor;

namespace {
void InitCinfo(jpeg_compress_struct* cinfo, jpeg_error_mgr* err,
               std::uint16_t width, std::uint16_t height, int jpeg_quality) {
  cinfo->err = jpeg_std_error(err);
  jpeg_create_compress(cinfo);

  cinfo->image_width = width;
  cinfo->image_height = height;
  cinfo->input_components = avd::vnc::BytesPerPixel();
  cinfo->in_color_space = JCS_EXT_RGBX;

  jpeg_set_defaults(cinfo);
  jpeg_set_quality(cinfo, jpeg_quality, true);
}
}  // namespace

avd::vnc::Message JpegCompressor::Compress(const Message& frame,
                                           int jpeg_quality, std::uint16_t x,
                                           std::uint16_t y, std::uint16_t width,
                                           std::uint16_t height,
                                           int screen_width) {
  jpeg_compress_struct cinfo{};
  jpeg_error_mgr err{};
  InitCinfo(&cinfo, &err, width, height, jpeg_quality);

  auto* compression_buffer = buffer_.get();
  auto compression_buffer_size = buffer_capacity_;
  jpeg_mem_dest(&cinfo, &compression_buffer, &compression_buffer_size);
  jpeg_start_compress(&cinfo, true);

  while (cinfo.next_scanline < cinfo.image_height) {
    auto row = static_cast<JSAMPROW>(const_cast<std::uint8_t*>(
        &frame[(y * screen_width * BytesPerPixel()) +
               (cinfo.next_scanline * BytesPerPixel() * screen_width) +
               (x * BytesPerPixel())]));
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  UpdateBuffer(compression_buffer, compression_buffer_size);
  return {compression_buffer, compression_buffer + compression_buffer_size};
}

void JpegCompressor::UpdateBuffer(std::uint8_t* compression_buffer,
                                  unsigned long compression_buffer_size) {
  if (buffer_capacity_ < compression_buffer_size) {
    ALOG_ASSERT(buffer_ != compression_buffer);
    buffer_capacity_ = compression_buffer_size;
    buffer_.reset(compression_buffer);
  } else {
    ALOG_ASSERT(buffer_ == compression_buffer);
  }
}
