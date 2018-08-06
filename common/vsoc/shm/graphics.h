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

// Memory layout for primitive graphics types.

// The vsoc::layout namespace indicates that these are shared memory structure
// definitions. The #include's given above are strictly limited, as are the
// types that can be referenced below.

#include <cstdint>

#include "common/vsoc/shm/base.h"

namespace vsoc {

// The enumerations for VSoC pixel formats are laid out so that hardware to
// parse bytes per pixel without relying an a exhaustive list of pixel formats.
// These constants define the fields involved.
namespace PixelFormatConst {
  static const uint32_t BytesPerPixelSize = 3;
  static const uint32_t SubformatSize = 3;
  static const uint32_t MaxBytesPerPixel = (1 << BytesPerPixelSize);
  static const uint32_t MaxSubformat = (1 << SubformatSize) - 1;
};


// Builds (statically) a new pixel format enumeration value given constant
// bytes per pixel.
template <uint32_t BYTES, uint32_t SUB_FORMAT>
struct PixelFormatBuilder {
  static_assert(BYTES > 0, "Too few bytes");
  static_assert(BYTES <= PixelFormatConst::MaxBytesPerPixel, "Too many bytes");
  static_assert(SUB_FORMAT <= PixelFormatConst::MaxSubformat,
                "Too many subformats");
  static const uint32_t value = ((BYTES - 1) << PixelFormatConst::SubformatSize) | SUB_FORMAT;
};

template <uint32_t FORMAT>
struct PixelFormatProperties {
  // No static asserts since all int32_t values are (technically) valid pixel formats?
  static const uint32_t bytes_per_pixel = (FORMAT >> PixelFormatConst::SubformatSize) + 1;
};

// Contains all of the pixel formats currently supported by this VSoC. The
// enumeration serves multiple purposes:
//
//   * The compile will warn (or error) if we switch on PixelFormat and don't
//     handly all of the cases.
//
//   * Code can use PixelFormat to describe paramaters, making APIs a bit more
//     self-documenting.
//
//   * Observant reviewers can verify that the same pixel value is not assigned
//     to multiple formats. Keep the enums in numerical order below to
//     make this easier.
enum PixelFormat : uint32_t {
  VSOC_PIXEL_FORMAT_UNINITIALIZED = PixelFormatBuilder<1,0>::value,
  VSOC_PIXEL_FORMAT_BLOB =          PixelFormatBuilder<1,1>::value,

  VSOC_PIXEL_FORMAT_RGB_565 =       PixelFormatBuilder<2,0>::value,
  VSOC_PIXEL_FORMAT_YV12 =          PixelFormatBuilder<2,1>::value,
  VSOC_PIXEL_FORMAT_YCbCr_420_888 = PixelFormatBuilder<2,2>::value,

  VSOC_PIXEL_FORMAT_RGB_888 =       PixelFormatBuilder<3,0>::value,

  VSOC_PIXEL_FORMAT_RGBA_8888 =     PixelFormatBuilder<4,0>::value,
  VSOC_PIXEL_FORMAT_RGBX_8888 =     PixelFormatBuilder<4,1>::value,
  VSOC_PIXEL_FORMAT_BGRA_8888 =     PixelFormatBuilder<4,2>::value,

  VSOC_PIXEL_FORMAT_RGBA_FP16 =     PixelFormatBuilder<8,0>::value,

  // VSOC_PIXEL_FORMAT_IMPLEMENTATION_DEFINED intentionally left out. The HALs
  // should choose one of the defined contrete types.
  //
  // The following formats are defined in various platform versions, but don't
  // seem to be used. If we encounter them it's ok to add them to the table.
  // This does not necessitate a version change.
  //
  // The following have been in the framework for a long time:
  //
  //   VSOC_PIXEL_FORMAT_YCrCb_420_SP
  //   VSOC_PIXEL_FORMAT_YCbCr_422_SP
  //
  // The following were added in JB_MR2:
  //
  //   VSOC_PIXEL_FORMAT_YCbCr_420_888
  //   VSOC_PIXEL_FORMAT_Y8
  //   VSOC_PIXEL_FORMAT_Y16
  //
  // The following were added in L:
  //
  //    VSOC_PIXEL_FORMAT_RAW_OPAQUE
  //    VSOC_PIXEL_FORMAT_RAW16 (also known as RAW_SENSOR. Define only RAW16)
  //    VSOC_PIXEL_FORMAT_RAW10
  //
  // The following were added in L MR1:
  //
  //   VSOC_PIXEL_FORMAT_YCbCr_444_888
  //   VSOC_PIXEL_FORMAT_YCbCr_422_888
  //   VSOC_PIXEL_FORMAT_RAW12
  //   VSOC_PIXEL_FORMAT_FLEX_RGBA_8888
  //   VSOC_PIXEL_FORMAT_FLEX_RGB_888
  //
  // These pixel formats were removed in later framework versions. Implement
  // only if absolutely necessary.
  //
  // Support was dropped in K for:
  //
  //   VSOC_PIXEL_FORMAT_RGBA_5551
  //   VSOC_PIXEL_FORMAT_RGBA_4444
  //
  // Supported only in K, L, and LMR1:
  //
  //   VSOC_PIXEL_FORMAT_sRGB_X_8888
  //   VSOC_PIXEL_FORMAT_sRGB_A_8888
};
// Enums can't have static members, so can't use the macro here.
static_assert(ShmTypeValidator<PixelFormat, 4>::valid,
              "Compilation error. Please fix above errors and retry.");

namespace layout {

// VSoC memory layout for a register that accepts a single pixel format.
// The value is volatile to ensure that the compiler does not eliminate stores.
struct PixelFormatRegister {
  static constexpr size_t layout_size = 4;

  volatile PixelFormat value_;
};
ASSERT_SHM_COMPATIBLE(PixelFormatRegister);

// Register layout for a mask giving different PixelFormats. Reserve enough
// space to allow for future expansion. For example, we may well end with
// a 12 bit per channel format in the future.
struct PixelFormatMaskRegister {
  static constexpr size_t layout_size = 8;

  volatile uint64_t value_;

  bool HasValue(PixelFormat in) {
    return !!(value_ & (uint64_t(1) << in));
  }
};
ASSERT_SHM_COMPATIBLE(PixelFormatMaskRegister);

// Ensure that the mask is large enough to hold the highest encodable
// pixel format.
static_assert(PixelFormatBuilder<
              PixelFormatConst::MaxBytesPerPixel,
              PixelFormatConst::MaxSubformat>::value <
              8 * sizeof(PixelFormatMaskRegister),
              "Largest pixel format does not fit in mask");
}  // layout
}  // vsoc
