
/*
 * Copyright 2018 Google LLC. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

// This file contains an adaptation of the algorithm at:
// https://github.com/GoogleChromeLabs/wasm-av1/blob/master/yuv-to-rgb.c

// The algorithm here creates precomputed lookup tables to speed up converting
// YUV frames to RGB. Since it is done once every camera frame it needs to be
// efficient.
//
// NOTE: This is code is being used temporarily until Cuttlefish supports
// hardware-accelerated camera frame transfer from host to guest.  Ideally the
// conversions will be done via DMA or GPU algorithms, not via CPU copy

// Number of luminance values to precompute tables of for speed. Value is higher
// than 255 as to allow for future color depth expansion
#define ZOF_TAB 65536

// Size of single output pixel in bytes (RGBA x 1 byte each = 4 bytes)
#define ZOF_RGB 4

namespace cuttlefish {

// These tables will store precomputes values
static int T1[ZOF_TAB], T2[ZOF_TAB], T3[ZOF_TAB], T4[ZOF_TAB];
static int tables_initialized;

// Called once to initialize tables
static void build_yuv2rgb_tables() {
  for (int i = 0; i < ZOF_TAB; i++) {
    T1[i] = (int)(1.370705 * (float)(i - 128));
    T2[i] = (int)(-0.698001 * (float)(i - 128));
    T3[i] = (int)(-0.337633 * (float)(i - 128));
    T4[i] = (int)(1.732446 * (float)(i - 128));
  }
}

#define clamp(val) ((val) < 0 ? 0 : (255 < (val) ? 255 : (val)))

void Yuv2Rgb(unsigned char *src, unsigned char *dst, int width, int height) {
  if (tables_initialized == 0) {
    tables_initialized = !0;
    build_yuv2rgb_tables();
  }
  // Setup pointers to the Y, U, V planes
  unsigned char *y = src;
  unsigned char *u = src + (width * height);
  unsigned char *v =
      u + (width * height) / 4;  // Each chroma does 4 pixels in 4:2:0
  // Loop the image, taking into account sub-sample for the chroma channels
  for (int h = 0; h < height; h++) {
    unsigned char *uline = u;
    unsigned char *vline = v;
    for (int w = 0; w < width; w++, y++) {
      int r = *y + T1[*vline];
      int g = *y + T2[*vline] + T3[*uline];
      int b = *y + T4[*uline];
      // Note: going BGRA here not RGBA
      dst[0] = clamp(b);  // 16-bit to 8-bit, chuck precision
      dst[1] = clamp(g);
      dst[2] = clamp(r);
      dst[3] = 255;
      dst += ZOF_RGB;
      if (w & 0x01) {
        uline++;
        vline++;
      }
    }
    if (h & 0x01) {
      u += width / 2;
      v += width / 2;
    }
  }
}

}  // End namespace cuttlefish