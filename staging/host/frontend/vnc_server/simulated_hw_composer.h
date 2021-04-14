#pragma once

/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <condition_variable>
#include <mutex>
#ifdef FUZZ_TEST_VNC
#include <random>
#endif
#include <thread>
#include <deque>

#include "common/libs/concurrency/thread_annotations.h"
#include "common/libs/concurrency/thread_safe_queue.h"
#include "host/frontend/vnc_server/blackboard.h"
#include "host/frontend/vnc_server/vnc_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/screen_connector/screen_connector.h"

namespace cuttlefish {
namespace vnc {
class SimulatedHWComposer {
 public:
  using GenerateProcessedFrameCallback = ScreenConnector::GenerateProcessedFrameCallback;

  SimulatedHWComposer(BlackBoard* bb, ScreenConnector& screen_connector);
  SimulatedHWComposer(const SimulatedHWComposer&) = delete;
  SimulatedHWComposer& operator=(const SimulatedHWComposer&) = delete;
  ~SimulatedHWComposer();

  Stripe GetNewStripe();

  void ReportClientsConnected();

  // NOTE not constexpr on purpose
  static int NumberOfStripes();

 private:
  bool closed();
  void close();
  static void EraseHalfOfElements(ThreadSafeQueue<Stripe>::QueueImpl* q);
  void MakeStripes();
  GenerateProcessedFrameCallback GetScreenConnectorCallback();

#ifdef FUZZ_TEST_VNC
  std::default_random_engine engine_;
  std::uniform_int_distribution<int> random_ =
      std::uniform_int_distribution<int>{0, 2};
#endif
  static constexpr int kNumStripes = 8;
  constexpr static std::size_t kMaxQueueElements = 64;
  bool closed_ GUARDED_BY(m_){};
  std::mutex m_;
  BlackBoard* bb_{};
  ThreadSafeQueue<Stripe> stripes_;
  std::thread stripe_maker_;
  ScreenConnector& screen_connector_;
};
}  // namespace vnc
}  // namespace cuttlefish
