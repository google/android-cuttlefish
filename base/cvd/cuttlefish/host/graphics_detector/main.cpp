/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <google/protobuf/text_format.h>

#include <fstream>
#include <string>

#include "cuttlefish/host/graphics_detector/graphics_detector.h"
#include "cuttlefish/host/graphics_detector/graphics_detector.pb.h"

namespace {

bool WriteToFile(const std::string& filename, const std::string& contents) {
  std::ofstream ofs(filename);
  if (!ofs.is_open()) {
    return false;
  }
  ofs << contents;
  ofs.close();
  return !ofs.fail();
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto availability = ::gfxstream::DetectGraphicsAvailability();

  std::string availabilityString;
  if (!google::protobuf::TextFormat::PrintToString(availability,
                                                   &availabilityString)) {
    std::cerr << "Failed to convert availability to string." << std::endl;
    return -1;
  }

  if (argc > 1) {
    const std::string filename = argv[1];
    if (!WriteToFile(filename, availabilityString)) {
      std::cerr << "Failed to write to '" << filename << "'.";
      return -1;
    }
  } else {
    std::cout << availabilityString << std::endl;
  }

  return 0;
}