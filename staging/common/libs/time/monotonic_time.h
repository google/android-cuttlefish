/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <stdint.h>
#include <time.h>

namespace cuttlefish {
namespace time {

static const int64_t kNanosecondsPerSecond = 1000000000;

class TimeDifference {
 public:
  TimeDifference(time_t seconds, long nanoseconds, int64_t scale) :
      scale_(scale), truncated_(false) {
    ts_.tv_sec = seconds;
    ts_.tv_nsec = nanoseconds;
    if (scale_ == kNanosecondsPerSecond) {
      truncated_ = true;
      truncated_ns_ = 0;
    }
  }

  TimeDifference(const TimeDifference& in, int64_t scale) :
      scale_(scale), truncated_(false) {
    ts_ = in.GetTS();
    if (scale_ == kNanosecondsPerSecond) {
      truncated_ = true;
      truncated_ns_ = 0;
    } else if ((in.scale_ % scale_) == 0) {
      truncated_ = true;
      truncated_ns_ = ts_.tv_nsec;
    }
  }

  TimeDifference(const struct timespec& in, int64_t scale) :
      ts_(in), scale_(scale), truncated_(false) { }

  TimeDifference operator*(const uint32_t factor) {
    TimeDifference rval = *this;
    rval.ts_.tv_sec = ts_.tv_sec * factor;
    // Create temporary variable to hold the multiplied
    // nanoseconds so that no overflow is possible.
    // Nanoseconds must be in [0, 10^9) and so all are less
    // then 2^30. Even multiplied by the largest uint32
    // this will fit in a 64-bit int without overflow.
    int64_t tv_nsec = static_cast<int64_t>(ts_.tv_nsec) * factor;
    rval.ts_.tv_sec += (tv_nsec / kNanosecondsPerSecond);
    rval.ts_.tv_nsec = tv_nsec % kNanosecondsPerSecond;
    return rval;
  }

  TimeDifference operator+(const TimeDifference& other) const {
    struct timespec ret = ts_;
    ret.tv_nsec = (ts_.tv_nsec + other.ts_.tv_nsec) % 1000000000;
    ret.tv_sec = (ts_.tv_sec + other.ts_.tv_sec) +
                  (ts_.tv_nsec + other.ts_.tv_nsec) / 1000000000;
    return TimeDifference(ret, scale_ < other.scale_ ? scale_: other.scale_);
  }

  TimeDifference operator-(const TimeDifference& other) const {
    struct timespec ret = ts_;
    // Keeps nanoseconds positive and allow negative numbers only on
    // seconds.
    ret.tv_nsec = (1000000000 + ts_.tv_nsec - other.ts_.tv_nsec) % 1000000000;
    ret.tv_sec = (ts_.tv_sec - other.ts_.tv_sec) -
                  (ts_.tv_nsec < other.ts_.tv_nsec ? 1 : 0);
    return TimeDifference(ret, scale_ < other.scale_ ? scale_: other.scale_);
  }

  bool operator<(const TimeDifference& other) const {
    return ts_.tv_sec < other.ts_.tv_sec ||
           (ts_.tv_sec == other.ts_.tv_sec && ts_.tv_nsec < other.ts_.tv_nsec);
  }

  int64_t count() const {
    return ts_.tv_sec * (kNanosecondsPerSecond / scale_) + ts_.tv_nsec / scale_;
  }

  time_t seconds() const {
    return ts_.tv_sec;
  }

  long subseconds_in_ns() const {
    if (!truncated_) {
      truncated_ns_ = (ts_.tv_nsec / scale_) * scale_;
      truncated_ = true;
    }
    return truncated_ns_;
  }

  struct timespec GetTS() const {
    // We can't assume C++11, so avoid extended initializer lists.
    struct timespec rval = { ts_.tv_sec, subseconds_in_ns()};
    return rval;
  }

 protected:
  struct timespec ts_;
  int64_t scale_;
  mutable bool truncated_;
  mutable long truncated_ns_;
};

class MonotonicTimePoint {
 public:
  static MonotonicTimePoint Now() {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    // WARNING:
    // While we do have CLOCK_MONOTONIC_RAW, we can't depend on it until:
    // - ALL places relying on MonotonicTimePoint are fixed,
    // - pthread supports pthread_timewait_monotonic.
    //
    // This is currently observable as a LEGITIMATE problem while running
    // pthread_test. DO NOT revert this to CLOCK_MONOTONIC_RAW until test
    // passes.
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return MonotonicTimePoint(ts);
  }

  MonotonicTimePoint() {
    ts_.tv_sec = 0;
    ts_.tv_nsec = 0;
  }

  explicit MonotonicTimePoint(const struct timespec& ts) {
    ts_ = ts;
  }

  TimeDifference SinceEpoch() const {
    return TimeDifference(ts_, 1);
  }

  TimeDifference operator-(const MonotonicTimePoint& other) const {
    struct timespec rval;
    rval.tv_sec = ts_.tv_sec - other.ts_.tv_sec;
    rval.tv_nsec = ts_.tv_nsec - other.ts_.tv_nsec;
    if (rval.tv_nsec < 0) {
      --rval.tv_sec;
      rval.tv_nsec += kNanosecondsPerSecond;
    }
    return TimeDifference(rval, 1);
  }

  MonotonicTimePoint operator+(const TimeDifference& other) const {
    MonotonicTimePoint rval = *this;
    rval.ts_.tv_sec += other.seconds();
    rval.ts_.tv_nsec += other.subseconds_in_ns();
    if (rval.ts_.tv_nsec >= kNanosecondsPerSecond) {
      ++rval.ts_.tv_sec;
      rval.ts_.tv_nsec -= kNanosecondsPerSecond;
    }
    return rval;
  }

  bool operator==(const MonotonicTimePoint& other) const {
    return (ts_.tv_sec == other.ts_.tv_sec) &&
        (ts_.tv_nsec == other.ts_.tv_nsec);
  }

  bool operator!=(const MonotonicTimePoint& other) const {
    return !(*this == other);
  }

  bool operator<(const MonotonicTimePoint& other) const {
    return ((ts_.tv_sec - other.ts_.tv_sec) < 0) ||
        ((ts_.tv_sec == other.ts_.tv_sec) &&
         (ts_.tv_nsec < other.ts_.tv_nsec));
  }

  bool operator>(const MonotonicTimePoint& other) const {
    return other < *this;
  }

  bool operator<=(const MonotonicTimePoint& other) const {
    return !(*this > other);
  }

  bool operator>=(const MonotonicTimePoint& other) const {
    return !(*this < other);
  }

  MonotonicTimePoint& operator+=(const TimeDifference& other) {
    ts_.tv_sec += other.seconds();
    ts_.tv_nsec += other.subseconds_in_ns();
    if (ts_.tv_nsec >= kNanosecondsPerSecond) {
      ++ts_.tv_sec;
      ts_.tv_nsec -= kNanosecondsPerSecond;
    }
    return *this;
  }

  MonotonicTimePoint& operator-=(const TimeDifference& other) {
    ts_.tv_sec -= other.seconds();
    ts_.tv_nsec -= other.subseconds_in_ns();
    if (ts_.tv_nsec < 0) {
      --ts_.tv_sec;
      ts_.tv_nsec += kNanosecondsPerSecond;
    }
    return *this;
  }

  void ToTimespec(struct timespec* dest) const {
    *dest = ts_;
  }

 protected:
  struct timespec ts_;
};

class MonotonicTimePointFactory {
 public:
  static MonotonicTimePointFactory* GetInstance();

  virtual ~MonotonicTimePointFactory() { }

  virtual void FetchCurrentTime(MonotonicTimePoint* dest) const {
    *dest = MonotonicTimePoint::Now();
  }
};

class Seconds : public TimeDifference {
 public:
  explicit Seconds(const TimeDifference& difference) :
      TimeDifference(difference, kNanosecondsPerSecond) { }

  Seconds(int64_t seconds) :
      TimeDifference(seconds, 0, kNanosecondsPerSecond) { }
};

class Milliseconds : public TimeDifference {
 public:
  explicit Milliseconds(const TimeDifference& difference) :
      TimeDifference(difference, kScale) { }

  Milliseconds(int64_t ms) : TimeDifference(
      ms / 1000, (ms % 1000) * kScale, kScale) { }

 protected:
  static const int kScale = kNanosecondsPerSecond / 1000;
};

class Microseconds : public TimeDifference {
 public:
  explicit Microseconds(const TimeDifference& difference) :
      TimeDifference(difference, kScale) { }

  Microseconds(int64_t micros) : TimeDifference(
      micros / 1000000, (micros % 1000000) * kScale, kScale) { }

 protected:
  static const int kScale = kNanosecondsPerSecond / 1000000;
};

class Nanoseconds : public TimeDifference {
 public:
  explicit Nanoseconds(const TimeDifference& difference) :
      TimeDifference(difference, 1) { }
  Nanoseconds(int64_t ns) : TimeDifference(ns / kNanosecondsPerSecond,
                                           ns % kNanosecondsPerSecond, 1) { }
};

}  // namespace time
}  // namespace cuttlefish

/**
 * Legacy support for microseconds. Use MonotonicTimePoint in new code.
 */
static const int64_t kSecsToUsecs = static_cast<int64_t>(1000) * 1000;

static inline int64_t get_monotonic_usecs() {
  return cuttlefish::time::Microseconds(
      cuttlefish::time::MonotonicTimePoint::Now().SinceEpoch()).count();
}
