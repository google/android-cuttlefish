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

#include "Packetizer.h"

#include <https/RunLoop.h>

#include <memory>

#include <source/StreamingSource.h>

struct G711Packetizer : public Packetizer {

    using StreamingSource = android::StreamingSource;

    enum class Mode {
        ALAW,
        ULAW
    };
    explicit G711Packetizer(
            Mode mode,
            std::shared_ptr<RunLoop> runLoop,
            std::shared_ptr<StreamingSource> audioSource);

    uint32_t rtpNow() const override;

private:
    using SBuffer = android::SBuffer;

    Mode mMode;
    std::shared_ptr<RunLoop> mRunLoop;

    bool mFirstInTalkspurt;

    void packetize(const std::shared_ptr<SBuffer> &accessUnit, int64_t timeUs);
};


