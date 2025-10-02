/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/random.h"

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace cuttlefish {

std::string GenerateRandomString(const std::string& alphabet,
                                 const int length) {
  std::srand(std::time(0));
  std::vector<char> result(length);
  for(int i = 0; i < length; i++){
    result[i] = alphabet[std::rand() % alphabet.size()];
  }
  return std::string(result.begin(), result.end());
}
}
