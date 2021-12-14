//
// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

namespace cuttlefish {

// Constants representing a not known or not detectable value for different
// signal strength parameters.
constexpr int kRssiUnknownValue = 99;
constexpr int kBerUnknownValue = 99;
constexpr int kDbmUnknownValue = -1;
constexpr int kEcioUnknownValue = -1;
constexpr int kSnrUnknownValue = -1;

// Constants representing the range of values of different signal strength
// parameters.
constexpr auto kRssiRange = std::make_pair(4, 30);
constexpr auto kDbmRange = std::make_pair(-120, -4);
constexpr auto kRsrpRange = std::make_pair(-140, -44);

}  // namespace cuttlefish
