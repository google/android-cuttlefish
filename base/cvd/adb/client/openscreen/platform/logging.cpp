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

#include <platform/api/logging.h>

#include <android-base/logging.h>

#include "adb_trace.h"

namespace openscreen {

bool IsLoggingOn(LogLevel level, const char* file) {
    return true;
}

static android::base::LogSeverity OpenScreenLogLevelToAndroid(LogLevel level) {
    switch (level) {
        case LogLevel::kVerbose:
            return android::base::VERBOSE;
        case LogLevel::kInfo:
            return android::base::INFO;
        case LogLevel::kWarning:
            return android::base::WARNING;
        case LogLevel::kError:
            return android::base::ERROR;
        case LogLevel::kFatal:
            return android::base::FATAL;
    }
}

void LogWithLevel(LogLevel level, const char* file, int line, std::stringstream desc) {
    auto severity = OpenScreenLogLevelToAndroid(level);
    std::string msg = std::string("(") + file + ":" + std::to_string(line) + ") " + desc.str();

    // We never ignore a warning or worse (error and fatals).
    if (severity >= android::base::WARNING) {
        LOG(severity) << msg;
    } else {
        VLOG(MDNS_STACK) << msg;
    }
}

[[noreturn]] void Break() {
    LOG(FATAL) << "openscreen Break() called";
    abort(); // LOG(FATAL) isn't [[noreturn]].
}

}  // namespace openscreen
