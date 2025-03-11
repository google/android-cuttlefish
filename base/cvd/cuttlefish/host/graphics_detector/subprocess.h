/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <chrono>
#include <functional>
#include <string>

#include "cuttlefish/host/graphics_detector/expected.h"

namespace gfxstream {

// Runs the given function in a forked subprocess first to check for
// aborts/crashes/etc and then runs the given function in the current
// process if the subprocess check succeeded.
gfxstream::expected<Ok, std::string> DoWithSubprocessCheck(
    const std::function<gfxstream::expected<Ok, std::string>()>& function,
    std::chrono::milliseconds timeout = std::chrono::seconds(15));

}  // namespace gfxstream