/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <thread>

namespace cuttlefish {

class MetricsHostReceiver {
 private:
  bool is_metrics_enabled_;
  std::thread thread_;
  std::string metrics_queue_name_;

  void ServerLoop();
  // Send different Clearcut events based on the received message
  void ProcessMessage(const std::string& text);

 public:
  MetricsHostReceiver(bool is_metrics_enabled);
  ~MetricsHostReceiver();
  bool Initialize(const std::string& metrics_queue_name);
  void Join();
};

}  // namespace cuttlefish
