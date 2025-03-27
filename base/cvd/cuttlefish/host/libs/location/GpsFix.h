/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <time.h>

#include <string>
#include <vector>

// A struct representing a location on a map
struct GpsFix {
  std::string name;
  std::string description;
  float latitude = 0.0;
  float longitude = 0.0;
  float elevation = 0.0;
  time_t time = 0;

  bool operator<(const GpsFix &other) const { return time < other.time; }
};

typedef std::vector<GpsFix> GpsFixArray;
