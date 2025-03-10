/*
 * Copyright (C) 2015-2022 The Android Open Source Project
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

#include "GpsFix.h"

#include <string>

class KmlParser {
 public:
  // Parses a given .kml file at |filePath| and extracts all contained GPS
  // fixes into |*fixes|.
  // Returns true on success, false otherwise. if false is returned, |*error|
  // is set to a message describing the error.
  static bool parseFile(const char* filePath, GpsFixArray* fixes,
                        std::string* error);
  static bool parseString(const char* str, int len, GpsFixArray* fixes,
                          std::string* error);
};