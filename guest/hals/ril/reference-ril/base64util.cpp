/*
**
** Copyright 2020, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include "base64util.h"

#include <vector>

#include "common/libs/utils/base64.h"

extern "C" {

int base64_decode(const char *base64input, unsigned char *bindata) {
  if (!base64input || !bindata) {
    return 0;
  }

  std::vector<uint8_t> output;
  std::string input(base64input);
  bool success = cuttlefish::DecodeBase64(input, &output);
  if (!success) {
    return 0;
  }
  memcpy(bindata, reinterpret_cast<unsigned char *>(output.data()),
         output.size());
  return output.size();
}

char *base64_encode(const unsigned char *bindata, char *base64output,
                    int binlength) {
  if (!base64output || !bindata || binlength <= 0) {
    return NULL;
  }

  std::string output;
  bool success = cuttlefish::EncodeBase64(
      bindata, static_cast<size_t>(binlength), &output);

  if (!success) {
    return NULL;
  }

  memcpy(base64output, output.data(), output.size());
  return base64output;
}
}
