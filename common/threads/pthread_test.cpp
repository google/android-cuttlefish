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
#include "common/threads/pthread.h"

#include <glog/logging.h>

#include "common/threads/thunkers.h"
#include "common/time/monotonic_time.h"

using avd::ConditionVariable;
using avd::Mutex;
using avd::ScopedThread;
using avd::time::MonotonicTimePoint;
using avd::time::Milliseconds;

static const int FINISHED = 100;

static void SleepUntil(const MonotonicTimePoint& in) {
  struct timespec ts;
  in.ToTimespec(&ts);
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

class MutexTest {
 public:
  MutexTest() : busy_(NULL), stage_(0) {}

  void Run() {
    {
      ScopedThread thread_a(
          MutexTestThunker<void*()>::call<&MutexTest::FastThread>, this);
      ScopedThread thread_b(
          MutexTestThunker<void*()>::call<&MutexTest::SlowThread>, this);
    }
    printf("MutexTest: completed at stage %d (%s)\n",
           stage_, (stage_ == FINISHED) ? "PASSED" : "FAILED");
  }

protected:
  template <typename F> struct MutexTestThunker :
  ThunkerBase<void, MutexTest, F>{};

  void* FastThread() {
    mutex_.Lock();
    LOG_ALWAYS_FATAL_IF(busy_ != NULL);
    busy_ = "FastThread";
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(100));
    stage_ = 1;
    busy_ = NULL;
    mutex_.Unlock();
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(10));
    mutex_.Lock();
    LOG_ALWAYS_FATAL_IF(busy_ != NULL);
    busy_ = "FastThread";
    LOG_ALWAYS_FATAL_IF(stage_ != 2);
    stage_ = FINISHED;
    busy_ = NULL;
    mutex_.Unlock();
    return NULL;
  }

  void* SlowThread() {
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(50));
    mutex_.Lock();
    LOG_ALWAYS_FATAL_IF(busy_ != NULL);
    busy_ = "SlowThread";
    LOG_ALWAYS_FATAL_IF(stage_ != 1);
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(100));
    stage_ = 2;
    busy_ = NULL;
    mutex_.Unlock();
    return NULL;
  }

  Mutex mutex_;
  const char* busy_;
  int stage_;
};

class NotifyOneTest {
 public:
  NotifyOneTest() : cond_(&mutex_), signalled_(0) {}

  void Run() {
    {
      ScopedThread thread_s(
          Thunker<void*()>::call<&NotifyOneTest::SignalThread>, this);
      ScopedThread thread_w1(
          Thunker<void*()>::call<&NotifyOneTest::WaitThread>, this);
      ScopedThread thread_w2(
          Thunker<void*()>::call<&NotifyOneTest::WaitThread>, this);
    }
    printf("NotifyOneTest: completed, signalled %d (%s)\n",
           signalled_, (signalled_ == 2) ? "PASSED" : "FAILED");
  }

protected:
  template <typename F> struct Thunker :
  ThunkerBase<void, NotifyOneTest, F>{};

  void* SignalThread() {
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(100));
    mutex_.Lock();
    cond_.NotifyOne();
    mutex_.Unlock();
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(100));
    mutex_.Lock();
    LOG_ALWAYS_FATAL_IF(signalled_ != 1);
    cond_.NotifyOne();
    mutex_.Unlock();
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(100));
    mutex_.Lock();
    LOG_ALWAYS_FATAL_IF(signalled_ != 2);
    mutex_.Unlock();
    return NULL;
  }

  void* WaitThread() {
    mutex_.Lock();
    cond_.Wait();
    signalled_++;
    mutex_.Unlock();
    return NULL;
  }

  Mutex mutex_;
  ConditionVariable cond_;
  int signalled_;
};

class NotifyAllTest {
 public:
  NotifyAllTest() : cond_(&mutex_), signalled_(0) {}

  void Run() {
    {
      ScopedThread thread_s(
          Thunker<void*()>::call<&NotifyAllTest::SignalThread>, this);
      ScopedThread thread_w1(
          Thunker<void*()>::call<&NotifyAllTest::WaitThread>, this);
      ScopedThread thread_w2(
          Thunker<void*()>::call<&NotifyAllTest::WaitThread>, this);
    }
    printf("NotifyAllTest: completed, signalled %d (%s)\n",
           signalled_, (signalled_ == 2) ? "PASSED" : "FAILED");
  }

protected:
  template <typename F> struct Thunker :
  ThunkerBase<void, NotifyAllTest, F>{};

  void* SignalThread() {
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(100));
    mutex_.Lock();
    cond_.NotifyAll();
    mutex_.Unlock();
    SleepUntil(MonotonicTimePoint::Now() + Milliseconds(100));
    mutex_.Lock();
    LOG_ALWAYS_FATAL_IF(signalled_ != 2);
    mutex_.Unlock();
    return NULL;
  }

  void* WaitThread() {
    mutex_.Lock();
    cond_.Wait();
    signalled_++;
    mutex_.Unlock();
    return NULL;
  }

  Mutex mutex_;
  ConditionVariable cond_;
  int signalled_;
};

class WaitUntilTest {
 public:
  WaitUntilTest() : cond_(&mutex_), stage_(0) {}

  void Run() {
    start_ = MonotonicTimePoint::Now();
    {
      ScopedThread thread_s(
          Thunker<void*()>::call<&WaitUntilTest::SignalThread>, this);
      ScopedThread thread_w2(
          Thunker<void*()>::call<&WaitUntilTest::WaitThread>, this);
    }
    printf("WaitUntilTest: completed, stage %d (%s)\n",
           stage_, (stage_ == FINISHED) ? "PASSED" : "FAILED");
  }

protected:
  template <typename F> struct Thunker :
  ThunkerBase<void, WaitUntilTest, F>{};

  void* SignalThread() {
    SleepUntil(start_ + Milliseconds(200));
    mutex_.Lock();
    LOG_ALWAYS_FATAL_IF(stage_ != 2);
    cond_.NotifyOne();
    stage_ = 3;
    mutex_.Unlock();
    return NULL;
  }

  void* WaitThread() {
    mutex_.Lock();
    LOG_ALWAYS_FATAL_IF(stage_ != 0);
    stage_ = 1;
    cond_.WaitUntil(start_ + Milliseconds(50));
    MonotonicTimePoint current(MonotonicTimePoint::Now());
    LOG_ALWAYS_FATAL_IF(Milliseconds(current - start_).count() < 50);
    LOG_ALWAYS_FATAL_IF(Milliseconds(current - start_).count() > 100);
    stage_ = 2;
    cond_.WaitUntil(start_ + Milliseconds(1000));
    current = MonotonicTimePoint::Now();
    LOG_ALWAYS_FATAL_IF(Milliseconds(current - start_).count() > 500);
    LOG_ALWAYS_FATAL_IF(stage_ != 3);
    stage_ = FINISHED;
    mutex_.Unlock();
    return NULL;
  }

  Mutex mutex_;
  ConditionVariable cond_;
  int stage_;
  MonotonicTimePoint start_;
};

int main() {
  MutexTest mt;
  mt.Run();
  NotifyOneTest nt1;
  nt1.Run();
  NotifyAllTest nta;
  nta.Run();
  WaitUntilTest wu;
  wu.Run();
}
