/*
 * Copyright (C) 2023 The Android Open Source Project
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
#pragma once

#include <string>
#include <vector>

namespace cuttlefish {

void LoadRGBAFromBitmapFile(const std::string& filename, uint32_t* out_w,
                            uint32_t* out_h, std::vector<uint8_t>* out_pixels);

void SaveRGBAToBitmapFile(uint32_t w, uint32_t h, const uint8_t* rgba_pixels,
                          const std::string& filename = "");

void LoadYUV420FromBitmapFile(const std::string& filename, uint32_t* out_w,
                              uint32_t* out_h, std::vector<uint8_t>* out_y,
                              std::vector<uint8_t>* out_u,
                              std::vector<uint8_t>* out_v);

void FillWithColor(uint32_t width, uint32_t height, uint8_t red, uint8_t green,
                   uint8_t blue, uint8_t alpha,
                   std::vector<uint8_t>* out_pixels);

void ConvertRGBA8888ToYUV420(uint32_t width, uint32_t height,
                             const std::vector<uint8_t>& rgba_pixels,
                             std::vector<uint8_t>* out_y_pixels,
                             std::vector<uint8_t>* out_u_pixels,
                             std::vector<uint8_t>* out_v_pixels);

bool ImagesAreSimilar(uint32_t width, uint32_t height,
                      const std::vector<uint8_t>& image1_rgba8888,
                      const std::vector<uint8_t>& image2_rgba8888);

}  // namespace cuttlefish
