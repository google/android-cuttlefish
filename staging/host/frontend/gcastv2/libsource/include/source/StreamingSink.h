/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <cinttypes>
#include <memory>

namespace android {

// TODO(jemoreira): add support for multitouch
struct InputEvent{
    InputEvent(int32_t down, int32_t x, int32_t y) : down(down), x(x), y(y) {}
    int32_t down;
    int32_t x;
    int32_t y;
};

struct StreamingSink {
    explicit StreamingSink() = default;
    virtual ~StreamingSink() = default;

    virtual void onAccessUnit(const std::shared_ptr<InputEvent> &accessUnit) = 0;
};

}  // namespace android

