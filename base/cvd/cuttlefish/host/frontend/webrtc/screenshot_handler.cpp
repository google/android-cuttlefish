/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "cuttlefish/host/frontend/webrtc/screenshot_handler.h"

#include <stdint.h>
#include <stdio.h>
#include <strings.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/match.h"
#include "drm/drm_fourcc.h"
#include "jpeglib.h"
#include "libyuv.h"
#include "png.h"

#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<std::vector<uint8_t>> FrameToRgb(
    const std::shared_ptr<VideoFrameBuffer>& frame) {
  const int width = frame->width();
  const int height = frame->height();
  std::vector<uint8_t> rgb_frame(width * height * 3, 0);

  const uint32_t format = frame->PixelFormat();
  int convert_res = -1;
  if (format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888) {
    convert_res = libyuv::ARGBToRAW(frame->Data(), frame->Stride(),
                                    rgb_frame.data(), width * 3, width, height);
  } else if (format == DRM_FORMAT_ABGR8888 || format == DRM_FORMAT_XBGR8888) {
    convert_res = libyuv::ABGRToRAW(frame->Data(), frame->Stride(),
                                    rgb_frame.data(), width * 3, width, height);
  } else {
    return CF_ERRF("Unsupported pixel format for PNG screenshot: 0x{:x}",
                   format);
  }
  CF_EXPECT(convert_res == 0, "Failed to convert frame to RGB");
  return rgb_frame;
}

Result<void> PngScreenshot(const std::shared_ptr<VideoFrameBuffer>& frame,
                           const std::string& screenshot_path) {
  const int width = frame->width();
  const int height = frame->height();
  const std::vector<uint8_t> rgb_frame = CF_EXPECT(FrameToRgb(frame));
  FILE* outfile = fopen(screenshot_path.c_str(), "wb");
  CF_EXPECTF(outfile != NULL, "opening {} failed: {}", screenshot_path,
             StrError());
  absl::Cleanup close_file = [outfile]() { fclose(outfile); };

  png_structp png_ptr =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  CF_EXPECT(png_ptr != nullptr, "Failed to create png write struct");
  absl::Cleanup free_png_ptr = [&png_ptr]() {
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
  };

  png_infop info_ptr = png_create_info_struct(png_ptr);
  CF_EXPECT(info_ptr != nullptr, "Failed to create png info struct");
  absl::Cleanup free_info_ptr = [png_ptr, info_ptr]() {
    png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
  };

  png_init_io(png_ptr, outfile);

  // Set header info
  png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
               PNG_FILTER_TYPE_BASE);

  png_write_info(png_ptr, info_ptr);

  for (int y = 0; y < height; ++y) {
    png_write_row(png_ptr, rgb_frame.data() + y * width * 3);
  }

  // Finalize
  png_write_end(png_ptr, info_ptr);

  return {};
}

Result<void> JpegScreenshot(const std::shared_ptr<VideoFrameBuffer>& frame,
                            const std::string& screenshot_path) {
  const int width = frame->width();
  const int height = frame->height();
  // libjpeg uses an MCU size of 16x16 so we require the stride to be a multiple
  // of 16 bytes and to have at least 16 rows (we'll use the previous rows as
  // padding if the height is not a multiple of 16).
  CF_EXPECTF(height >= 16,
             "Frame size not compatible with required MCU size of 16x16: {}x{}",
             width, height);

  constexpr int kPlanePadding = 1024;
  constexpr int kStrideAlignment = 64;
  const int stride_y =
      ((width + kStrideAlignment - 1) / kStrideAlignment) * kStrideAlignment;
  const int stride_uv =
      (((width + 1) / 2 + kStrideAlignment - 1) / kStrideAlignment) *
      kStrideAlignment;
  std::vector<uint8_t> y_plane(stride_y * height + kPlanePadding);
  std::vector<uint8_t> u_plane(stride_uv * ((height + 1) / 2) + kPlanePadding);
  std::vector<uint8_t> v_plane(stride_uv * ((height + 1) / 2) + kPlanePadding);

  const uint32_t format = frame->PixelFormat();
  int convert_res = -1;
  if (format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888) {
    convert_res = libyuv::ARGBToI420(
        frame->Data(), frame->Stride(), y_plane.data(), stride_y,
        u_plane.data(), stride_uv, v_plane.data(), stride_uv, width, height);
  } else if (format == DRM_FORMAT_ABGR8888 || format == DRM_FORMAT_XBGR8888) {
    convert_res = libyuv::ABGRToI420(
        frame->Data(), frame->Stride(), y_plane.data(), stride_y,
        u_plane.data(), stride_uv, v_plane.data(), stride_uv, width, height);
  } else {
    return CF_ERRF("Unsupported pixel format for JPEG screenshot: 0x{:x}",
                   format);
  }
  CF_EXPECT(convert_res == 0, "Failed to convert frame to I420");

  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  FILE* outfile; /* target file */

  // This actually causes libjpeg to exit on error, but that's better than the
  // recommended approach of jumping around goto-style. The only function that
  // could cause this is jpeg_write_raw_data, which is unlikely to fail anyways.
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  absl::Cleanup destroy_compress = [&cinfo]() {
    jpeg_destroy_compress(&cinfo);
  };

  CF_EXPECTF((outfile = fopen(screenshot_path.c_str(), "wb")) != NULL,
             "Failed to open screenshot destination ({})", screenshot_path);
  absl::Cleanup close_file = [&outfile]() { fclose(outfile); };
  jpeg_stdio_dest(&cinfo, outfile);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_YCbCr;
  jpeg_set_defaults(&cinfo);
  const int kJpegQuality = 100;
  jpeg_set_quality(&cinfo, kJpegQuality, true);
  // Frame is already in YCbCr format with the right downsampling.
  cinfo.raw_data_in = true;
  jpeg_set_colorspace(&cinfo, JCS_YCbCr);
  // jpeg_set_defaults should have set these, but libjpeg recommends setting
  // them manually anyways.
  cinfo.comp_info[0].h_samp_factor = 2;
  cinfo.comp_info[0].v_samp_factor = 2;
  cinfo.comp_info[1].h_samp_factor = 1;
  cinfo.comp_info[1].v_samp_factor = 1;
  cinfo.comp_info[2].h_samp_factor = 1;
  cinfo.comp_info[2].v_samp_factor = 1;

  // libjpeg accepts no less than 16 rows at a time
  constexpr int kScanRows = 16;
  JSAMPROW y_rows[kScanRows];
  JSAMPROW u_rows[kScanRows / 2];
  JSAMPROW v_rows[kScanRows / 2];
  JSAMPARRAY rows[]{y_rows, u_rows, v_rows};

  jpeg_start_compress(&cinfo, true);

  while (cinfo.next_scanline < cinfo.image_height) {
    JDIMENSION row = cinfo.next_scanline;
    // If the image height is not a multiple of kScanRows it will be padded with
    // rows from the previous iteration.
    for (int r = 0; r < kScanRows && r + row < cinfo.image_height; ++r) {
      int offset = (row + r) * stride_y;
      y_rows[r] = &y_plane[offset];
    }
    for (int r = 0;
         r < kScanRows / 2 && r + row / 2 < (cinfo.image_height + 1) / 2; ++r) {
      int offset_u = (row / 2 + r) * stride_uv;
      u_rows[r] = &u_plane[offset_u];
      int offset_v = (row / 2 + r) * stride_uv;
      v_rows[r] = &v_plane[offset_v];
    }
    jpeg_write_raw_data(&cinfo, rows, kScanRows);
  }

  jpeg_finish_compress(&cinfo);

  return {};
}

}  // namespace

Result<void> ScreenshotHandler::Screenshot(uint32_t display_number,
                                           const std::string& screenshot_path) {
  SharedFrameFuture frame_future;
  {
    std::lock_guard<std::mutex> lock(pending_screenshot_displays_mutex_);

    auto [it, inserted] = pending_screenshot_displays_.emplace(
        display_number, SharedFramePromise{});
    if (!inserted) {
      return CF_ERRF("Screenshot already pending for display {}",
                     display_number);
    }

    frame_future = it->second.get_future().share();
  }

  static constexpr const int kScreenshotTimeoutSeconds = 5;
  auto result =
      frame_future.wait_for(std::chrono::seconds(kScreenshotTimeoutSeconds));
  CF_EXPECT(result == std::future_status::ready,
            "Failed to get screenshot from webrtc display handler within "
                << kScreenshotTimeoutSeconds << " seconds.");

  const SharedFrame frame = frame_future.get();
  CF_EXPECT(frame != nullptr, "Received null frame for screenshot");

  if (absl::EndsWith(screenshot_path, ".jpg")) {
    CF_EXPECT(JpegScreenshot(frame, screenshot_path));
  } else if (absl::EndsWith(screenshot_path, ".png")) {
    CF_EXPECT(PngScreenshot(frame, screenshot_path));
  } else {
    return CF_ERR("Unsupport file format: " << screenshot_path);
  }
  return {};
}

void ScreenshotHandler::OnFrame(uint32_t display_number, SharedFrame& frame) {
  std::lock_guard<std::mutex> lock(pending_screenshot_displays_mutex_);

  auto pending_screenshot_it =
      pending_screenshot_displays_.find(display_number);
  if (pending_screenshot_it == pending_screenshot_displays_.end()) {
    return;
  }
  SharedFramePromise& frame_promise = pending_screenshot_it->second;

  frame_promise.set_value(frame);

  pending_screenshot_displays_.erase(pending_screenshot_it);
}

}  // namespace cuttlefish
