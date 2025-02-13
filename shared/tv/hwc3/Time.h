/*
 * Copyright 2022 The Android Open Source Project
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

#ifndef ANDROID_HWC_TIME_H
#define ANDROID_HWC_TIME_H

#include <utils/Timers.h>

#include <chrono>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

using Nanoseconds = std::chrono::nanoseconds;

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

inline TimePoint asTimePoint(int64_t nanos) {
  return TimePoint(Nanoseconds(nanos));
}

inline TimePoint now() {
  return asTimePoint(systemTime(SYSTEM_TIME_MONOTONIC));
}

inline int64_t asNanosDuration(Nanoseconds duration) {
  return duration.count();
}

inline int64_t asNanosTimePoint(TimePoint time) {
  TimePoint zero(Nanoseconds(0));
  return static_cast<int64_t>(
      std::chrono::duration_cast<Nanoseconds>(time - zero).count());
}

constexpr int32_t HertzToPeriodNanos(uint32_t hertz) {
  return 1000 * 1000 * 1000 / hertz;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif
