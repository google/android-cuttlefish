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

#include <atomic>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <json/json.h>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

// Listen to kernel log events and report them to clients.
struct KernelLogEventsHandler {
  explicit KernelLogEventsHandler(SharedFD kernel_log_fd);

  ~KernelLogEventsHandler();

  int AddSubscriber(std::function<void(const Json::Value&)> subscriber);
  void Unsubscribe(int subscriber_id);
 private:
  void ReadLoop();
  void DeliverEvent(const Json::Value& event);

  SharedFD kernel_log_fd_;
  SharedFD eventfd_;
  std::atomic<bool> running_;
  std::thread read_thread_;
  std::map<int, std::function<void(const Json::Value&)>> subscribers_;
  int last_subscriber_id_ = 0;
  std::mutex subscribers_mtx_;
  std::list<Json::Value> last_events_;
};

}  // namespace cuttlefish
