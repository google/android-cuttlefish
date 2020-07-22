//
// Copyright (C) 2020 The Android Open Source Project
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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace cuttlefish {

template <typename T>
std::function<void()> makeSafeCallback(T *me, std::function<void(T *)> f) {
  return [f, me] {
    if (me) {
      f(me);
    }
  };
}

template<typename T, typename... Params>
std::function<void()> makeSafeCallback(
    T *obj, void (T::*f)(const Params&...), const Params&... params) {
  return makeSafeCallback<T>(obj,
                             [f, params...](T *me) { (me->*f)(params...); });
}

template<typename T, typename... Params>
std::function<void()> makeSafeCallback(
      T *obj, void (T::*f)(Params...), const Params&... params) {
  return makeSafeCallback<T>(obj,
                             [f, params...](T *me) { (me->*f)(params...); });
}

class ThreadLooper {
 public:
  ThreadLooper();
  ~ThreadLooper();

  ThreadLooper(const ThreadLooper &) = delete;
  ThreadLooper &operator=(const ThreadLooper &) = delete;

  typedef std::function<void()> Callback;
  typedef int32_t Serial;

  Serial Post(Callback cb);
  Serial PostWithDelay(std::chrono::steady_clock::duration delay, Callback cb);

  void Stop();

  // Returns true if matching event was canceled.
  bool CancelSerial(Serial serial);

 private:
  struct Event {
      std::chrono::steady_clock::time_point when;
      Callback cb;
      Serial serial;

      bool operator<=(const Event &other) const;
  };

  bool stopped_;
  std::thread looper_thread_;

  std::mutex lock_;
  std::condition_variable cond_;
  std::deque<Event> queue_;
  std::atomic<Serial> next_serial_;

  void ThreadLoop();

  void Insert(const Event &event);
};

};  // namespace cuttlefish
