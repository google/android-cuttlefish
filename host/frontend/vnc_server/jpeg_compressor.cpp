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

#include <stdio.h>  // stdio.h must appear before jpeglib.h
#include <jpeglib.h>

#include <android-base/logging.h>
#include "host/frontend/vnc_server/jpeg_compressor.h"
#include "host/frontend/vnc_server/vnc_utils.h"
#include "host/libs/screen_connector/screen_connector.h"

using cvd::vnc::JpegCompressor;

namespace {
void InitCinfo(jpeg_compress_struct* cinfo, jpeg_error_mgr* err,
               std::uint16_t width, std::uint16_t height, int jpeg_quality) {
  cinfo->err = jpeg_std_error(err);
  jpeg_create_compress(cinfo);

  cinfo->image_width = width;
  cinfo->image_height = height;
  cinfo->input_components = cvd::ScreenConnector::BytesPerPixel();
  cinfo->in_color_space = JCS_EXT_RGBX;

  jpeg_set_defaults(cinfo);
  jpeg_set_quality(cinfo, jpeg_quality, true);
}
}  // namespace

cvd::Message JpegCompressor::Compress(const Message& frame,
                                      int jpeg_quality, std::uint16_t x,
                                      std::uint16_t y, std::uint16_t width,
                                      std::uint16_t height,
                                      int stride) {
  jpeg_compress_struct cinfo{};
  jpeg_error_mgr err{};
  InitCinfo(&cinfo, &err, width, height, jpeg_quality);

  auto* compression_buffer = buffer_.get();
  auto compression_buffer_size = buffer_capacity_;
  jpeg_mem_dest(&cinfo, &compression_buffer, &compression_buffer_size);
  jpeg_start_compress(&cinfo, true);

  while (cinfo.next_scanline < cinfo.image_height) {
    auto row = static_cast<JSAMPROW>(const_cast<std::uint8_t*>(
        &frame[(y * stride) +
               (cinfo.next_scanline * stride) +
               (x * cvd::ScreenConnector::BytesPerPixel())]));
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  UpdateBuffer(compression_buffer, compression_buffer_size);
  return {compression_buffer, compression_buffer + compression_buffer_size};
}

void JpegCompressor::UpdateBuffer(std::uint8_t* compression_buffer,
                                  unsigned long compression_buffer_size) {
  if (buffer_.get() != compression_buffer) {
    buffer_capacity_ = compression_buffer_size;
    buffer_.reset(compression_buffer);
  }
}
