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

struct GceFrameBuffer {
  typedef uint32_t Pixel;

  static const int kRedShift = 0;
  static const int kRedBits = 8;
  static const int kGreenShift = 8;
  static const int kGreenBits = 8;
  static const int kBlueShift = 16;
  static const int kBlueBits = 8;
  static const int kAlphaShift = 24;
  static const int kAlphaBits = 8;
};

// Sensors
struct gce_sensors_message {
  static constexpr const char* const kSensorsHALSocketName = "";
};
