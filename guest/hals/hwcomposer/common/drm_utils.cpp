/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "guest/hals/hwcomposer/common/drm_utils.h"

#include <drm_fourcc.h>
#include <log/log.h>
#include <system/graphics.h>

namespace cuttlefish {

const char* GetDrmFormatString(uint32_t drm_format) {
  switch (drm_format) {
    case DRM_FORMAT_ABGR1555:
      return "DRM_FORMAT_ABGR1555";
    case DRM_FORMAT_ABGR2101010:
      return "DRM_FORMAT_ABGR2101010";
    case DRM_FORMAT_ABGR4444:
      return "DRM_FORMAT_ABGR4444";
    case DRM_FORMAT_ABGR8888:
      return "DRM_FORMAT_ABGR8888";
    case DRM_FORMAT_ARGB1555:
      return "DRM_FORMAT_ARGB1555";
    case DRM_FORMAT_ARGB2101010:
      return "DRM_FORMAT_ARGB2101010";
    case DRM_FORMAT_ARGB4444:
      return "DRM_FORMAT_ARGB4444";
    case DRM_FORMAT_ARGB8888:
      return "DRM_FORMAT_ARGB8888";
    case DRM_FORMAT_AYUV:
      return "DRM_FORMAT_AYUV";
    case DRM_FORMAT_BGR233:
      return "DRM_FORMAT_BGR233";
    case DRM_FORMAT_BGR565:
      return "DRM_FORMAT_BGR565";
    case DRM_FORMAT_BGR888:
      return "DRM_FORMAT_BGR888";
    case DRM_FORMAT_BGRA1010102:
      return "DRM_FORMAT_BGRA1010102";
    case DRM_FORMAT_BGRA4444:
      return "DRM_FORMAT_BGRA4444";
    case DRM_FORMAT_BGRA5551:
      return "DRM_FORMAT_BGRA5551";
    case DRM_FORMAT_BGRA8888:
      return "DRM_FORMAT_BGRA8888";
    case DRM_FORMAT_BGRX1010102:
      return "DRM_FORMAT_BGRX1010102";
    case DRM_FORMAT_BGRX4444:
      return "DRM_FORMAT_BGRX4444";
    case DRM_FORMAT_BGRX5551:
      return "DRM_FORMAT_BGRX5551";
    case DRM_FORMAT_BGRX8888:
      return "DRM_FORMAT_BGRX8888";
    case DRM_FORMAT_C8:
      return "DRM_FORMAT_C8";
    case DRM_FORMAT_GR88:
      return "DRM_FORMAT_GR88";
    case DRM_FORMAT_NV12:
      return "DRM_FORMAT_NV12";
    case DRM_FORMAT_NV21:
      return "DRM_FORMAT_NV21";
    case DRM_FORMAT_R8:
      return "DRM_FORMAT_R8";
    case DRM_FORMAT_RG88:
      return "DRM_FORMAT_RG88";
    case DRM_FORMAT_RGB332:
      return "DRM_FORMAT_RGB332";
    case DRM_FORMAT_RGB565:
      return "DRM_FORMAT_RGB565";
    case DRM_FORMAT_RGB888:
      return "DRM_FORMAT_RGB888";
    case DRM_FORMAT_RGBA1010102:
      return "DRM_FORMAT_RGBA1010102";
    case DRM_FORMAT_RGBA4444:
      return "DRM_FORMAT_RGBA4444";
    case DRM_FORMAT_RGBA5551:
      return "DRM_FORMAT_RGBA5551";
    case DRM_FORMAT_RGBA8888:
      return "DRM_FORMAT_RGBA8888";
    case DRM_FORMAT_RGBX1010102:
      return "DRM_FORMAT_RGBX1010102";
    case DRM_FORMAT_RGBX4444:
      return "DRM_FORMAT_RGBX4444";
    case DRM_FORMAT_RGBX5551:
      return "DRM_FORMAT_RGBX5551";
    case DRM_FORMAT_RGBX8888:
      return "DRM_FORMAT_RGBX8888";
    case DRM_FORMAT_UYVY:
      return "DRM_FORMAT_UYVY";
    case DRM_FORMAT_VYUY:
      return "DRM_FORMAT_VYUY";
    case DRM_FORMAT_XBGR1555:
      return "DRM_FORMAT_XBGR1555";
    case DRM_FORMAT_XBGR2101010:
      return "DRM_FORMAT_XBGR2101010";
    case DRM_FORMAT_XBGR4444:
      return "DRM_FORMAT_XBGR4444";
    case DRM_FORMAT_XBGR8888:
      return "DRM_FORMAT_XBGR8888";
    case DRM_FORMAT_XRGB1555:
      return "DRM_FORMAT_XRGB1555";
    case DRM_FORMAT_XRGB2101010:
      return "DRM_FORMAT_XRGB2101010";
    case DRM_FORMAT_XRGB4444:
      return "DRM_FORMAT_XRGB4444";
    case DRM_FORMAT_XRGB8888:
      return "DRM_FORMAT_XRGB8888";
    case DRM_FORMAT_YUYV:
      return "DRM_FORMAT_YUYV";
    case DRM_FORMAT_YVU420:
      return "DRM_FORMAT_YVU420";
    case DRM_FORMAT_YVYU:
      return "DRM_FORMAT_YVYU";
  }
  return "Unknown";
}

int GetDrmFormatBytesPerPixel(uint32_t drm_format) {
  switch (drm_format) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XBGR8888:
      return 4;
    case DRM_FORMAT_BGR888:
      return 3;
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_YVU420:
#ifdef GRALLOC_MODULE_API_VERSION_0_2
    case DRM_FORMAT_FLEX_YCbCr_420_888:
#endif
      return 2;
    case DRM_FORMAT_R8:
      return 1;
  }
  ALOGE("%s: format size unknown %d(%s)", __FUNCTION__, drm_format,
        GetDrmFormatString(drm_format));
  return 8;
}

int GetDrmFormatFromHalFormat(int hal_format) {
  switch (hal_format) {
    case HAL_PIXEL_FORMAT_RGBA_FP16:
      return DRM_FORMAT_ABGR16161616F;
    case HAL_PIXEL_FORMAT_RGBA_8888:
      return DRM_FORMAT_ABGR8888;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      return DRM_FORMAT_XBGR8888;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      return DRM_FORMAT_ARGB8888;
    case HAL_PIXEL_FORMAT_RGB_888:
      return DRM_FORMAT_BGR888;
    case HAL_PIXEL_FORMAT_RGB_565:
      return DRM_FORMAT_BGR565;
    case HAL_PIXEL_FORMAT_YV12:
      return DRM_FORMAT_YVU420;
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      return DRM_FORMAT_YVU420;
    case HAL_PIXEL_FORMAT_BLOB:
      return DRM_FORMAT_R8;
    default:
      break;
  }
  ALOGE("%s unhandled hal format: %d", __FUNCTION__, hal_format);
  return 0;
}

}  // namespace cuttlefish
