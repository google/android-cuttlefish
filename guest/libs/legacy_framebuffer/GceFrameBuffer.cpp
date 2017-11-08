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
#include <api_level_fixes.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utils/String8.h>

#define LOG_TAG "GceFrameBuffer"
#include <cutils/log.h>
#include <system/graphics.h>
#include "GceMetadataAttributes.h"
#include "InitialMetadataReader.h"

#include "GceFrameBuffer.h"


const char* const GceFrameBuffer::kFrameBufferPath =
    "/dev/userspace_framebuffer";


const GceFrameBuffer & GceFrameBuffer::getInstance() {
  static GceFrameBuffer instance;
  instance.Configure();
  return instance;
}


GceFrameBuffer::GceFrameBuffer()
    : line_length_(-1) { }


void GceFrameBuffer::Configure() {
  const char* metadata_value =
      avd::InitialMetadataReader::getInstance()->GetValueForKey(
          GceMetadataAttributes::kDisplayConfigurationKey);
  display_properties_.Parse(metadata_value);
  line_length_ = align(
      display_properties_.GetXRes() * (
          display_properties_.GetBitsPerPixel() / 8));
}


bool GceFrameBuffer::OpenFrameBuffer(int* frame_buffer_fd) {
  int fb_fd;
  if ((fb_fd = open(GceFrameBuffer::kFrameBufferPath, O_RDWR)) < 0) {
    SLOGE("Failed to open '%s' (%s)",
          GceFrameBuffer::kFrameBufferPath, strerror(errno));
    return false;
  }

  const GceFrameBuffer& config = GceFrameBuffer::getInstance();

  if (ftruncate(fb_fd, config.total_buffer_size()) < 0) {
    SLOGE("Failed to truncate framebuffer (%s)", strerror(errno));
    return false;
  }

  *frame_buffer_fd = fb_fd;
  return true;
}


bool GceFrameBuffer::OpenAndMapFrameBuffer(void** fb_memory,
                                                  int* frame_buffer_fd) {
  int fb_fd;
  if (!GceFrameBuffer::OpenFrameBuffer(&fb_fd)) { return false; }

  size_t fb_size = GceFrameBuffer::getInstance().total_buffer_size();

  void* mmap_res = mmap(0, fb_size, PROT_READ, MAP_SHARED, fb_fd, 0);
  if (mmap_res == MAP_FAILED) {
    SLOGE("Failed to mmap framebuffer (%s)", strerror(errno));
    close(fb_fd);
    return false;
  }

  // Modify the pointers only after mmap succeeds.
  *fb_memory = mmap_res;
  *frame_buffer_fd = fb_fd;

  return true;
}

bool GceFrameBuffer::UnmapAndCloseFrameBuffer(void* fb_memory,
                                                    int frame_buffer_fd) {
  size_t fb_size = GceFrameBuffer::getInstance().total_buffer_size();
  return munmap(fb_memory, fb_size) == 0 && close(frame_buffer_fd) == 0;
}


int GceFrameBuffer::hal_format() const {
  switch(display_properties_.GetBitsPerPixel()) {
    case 32:
      if (kRedShift) {
        return HAL_PIXEL_FORMAT_BGRA_8888;
      } else {
        return HAL_PIXEL_FORMAT_RGBX_8888;
      }
    default:
      return HAL_PIXEL_FORMAT_RGB_565;
  }
}

const char* pixel_format_to_string(int format) {
  switch (format) {
    // Formats that are universal across versions
    case HAL_PIXEL_FORMAT_RGBA_8888:
      return "RGBA_8888";
    case HAL_PIXEL_FORMAT_RGBX_8888:
      return "RGBX_8888";
    case HAL_PIXEL_FORMAT_BGRA_8888:
      return "BGRA_8888";
    case HAL_PIXEL_FORMAT_RGB_888:
      return "RGB_888";
    case HAL_PIXEL_FORMAT_RGB_565:
      return "RGB_565";
    case HAL_PIXEL_FORMAT_YV12:
      return "YV12";
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      return "YCrCb_420_SP";
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
      return "YCbCr_422_SP";
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
      return "YCbCr_422_I";

#if GCE_PLATFORM_SDK_AFTER(J)
    // First supported on JBMR1 (API 17)
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      return "IMPLEMENTATION_DEFINED";
    case HAL_PIXEL_FORMAT_BLOB:
      return "BLOB";
#endif
#if GCE_PLATFORM_SDK_AFTER(J_MR1)
    // First supported on JBMR2 (API 18)
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      return "YCbCr_420_888";
    case HAL_PIXEL_FORMAT_Y8:
      return "Y8";
    case HAL_PIXEL_FORMAT_Y16:
      return "Y16";
#endif
#if GCE_PLATFORM_SDK_AFTER(K)
    // Support was added in L (API 21)
    case HAL_PIXEL_FORMAT_RAW_OPAQUE:
      return "RAW_OPAQUE";
    // This is an alias for RAW_SENSOR in L and replaces it in M.
    case HAL_PIXEL_FORMAT_RAW16:
      return "RAW16";
    case HAL_PIXEL_FORMAT_RAW10:
      return "RAW10";
#endif
#if GCE_PLATFORM_SDK_AFTER(L_MR1)
    case HAL_PIXEL_FORMAT_YCbCr_444_888:
      return "YCbCr_444_888";
    case HAL_PIXEL_FORMAT_YCbCr_422_888:
      return "YCbCr_422_888";
    case HAL_PIXEL_FORMAT_RAW12:
      return "RAW12";
    case HAL_PIXEL_FORMAT_FLEX_RGBA_8888:
      return "FLEX_RGBA_8888";
    case HAL_PIXEL_FORMAT_FLEX_RGB_888:
      return "FLEX_RGB_888";
#endif

    // Formats that have been removed
#if GCE_PLATFORM_SDK_BEFORE(K)
    // Support was dropped on K (API 19)
    case HAL_PIXEL_FORMAT_RGBA_5551:
      return "RGBA_5551";
    case HAL_PIXEL_FORMAT_RGBA_4444:
      return "RGBA_4444";
#endif
#if GCE_PLATFORM_SDK_BEFORE(L)
    // Renamed to RAW_16 in L. Both were present for L, but it was completely
    // removed in M.
    case HAL_PIXEL_FORMAT_RAW_SENSOR:
      return "RAW_SENSOR";
#endif
#if GCE_PLATFORM_SDK_AFTER(J_MR2) && GCE_PLATFORM_SDK_BEFORE(M)
    // Supported K, L, and LMR1. Not supported on JBMR0, JBMR1, JBMR2, and M
    case HAL_PIXEL_FORMAT_sRGB_X_8888:
      return "sRGB_X_8888";
    case HAL_PIXEL_FORMAT_sRGB_A_8888:
      return "sRGB_A_8888";
#endif
  }
  return "UNKNOWN";
}
