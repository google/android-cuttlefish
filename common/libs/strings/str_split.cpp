/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/strings/str_split.h"

#include <vector>
#include <string>
#include <sstream>

std::vector<std::string> cvd::StrSplit(const std::string& src, char delimiter) {
  std::istringstream stream{src};
  std::vector<std::string> result;
  for (std::string s; std::getline(stream, s, delimiter); s.clear()) {
    result.push_back(std::move(s));
  }
  return result;
}
