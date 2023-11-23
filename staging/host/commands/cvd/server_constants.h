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

#pragma once

#include <string>

namespace cuttlefish {
namespace cvd {

// Major version uprevs are backwards incompatible.
// Minor version uprevs are backwards compatible within major version.
constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 4;

}  // namespace cvd

// Pathname of the abstract cvd_server socket.
std::string ServerSocketPath();

}  // namespace cuttlefish
