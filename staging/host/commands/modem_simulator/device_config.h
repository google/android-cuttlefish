/*
 * Copyright (C) 2020 The Android Open Source Project
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
#include <fstream>
 
// this file provide a few device (cvd or emulator) specific hooks for
// modem-simulator

namespace cuttlefish {
namespace modem {

class DeviceConfig {
 public:
  static int host_id();
  static std::string PerInstancePath(const char* file_name);
  static std::string DefaultHostArtifactsPath(const std::string& file);
  static std::string ril_address_and_prefix();
  static std::string ril_gateway();
  static std::string ril_dns();
  static std::ifstream open_ifstream_crossplat(const char* filename);
  static std::ofstream open_ofstream_crossplat(const char* filename, std::ios_base::openmode mode = std::ios_base::out);
};

}  // namespace modem
}  // namespace cuttlefish
