//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/android_build_api_key.h"

#include <string>
#include <string_view>

namespace cuttlefish {
namespace {

// only provides access to already public resources from V4 Android Build API
// for example, AOSP builds from ci.android.com
constexpr std::string_view kApiKey = "AIzaSyBIelMvbjtNkpa5O96eqbm_IuSUA5WsO14";

}  // namespace

std::string GetCatchallApiKey() { return std::string(kApiKey); }

}  // namespace cuttlefish
