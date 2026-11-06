// Copyright 2026, The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use v4l2r::PixelFormat;

/// `V4L2_PIX_FMT_YUV420M` (`YM12`): 8-bit multi-planar YUV 4:2:0.
pub struct Yuv420m;

impl Yuv420m {
    /// FourCC as reported through `VIDIOC_ENUM_FMT` and `VIDIOC_G_FMT`.
    pub const PIXELFORMAT: u32 = PixelFormat::from_fourcc(b"YM12").to_u32();

    /// Y, U and V.
    pub const NUM_PLANES: usize = 3;

    /// Sizes in bytes of the Y, U and V planes of a `width`x`height` frame.
    pub const fn plane_sizes(width: u32, height: u32) -> [u32; Self::NUM_PLANES] {
        let luma = width * height;
        // Chroma is subsampled by two in both directions.
        let chroma = luma / 4;
        [luma, chroma, chroma]
    }
}
