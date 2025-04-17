/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include <thread>

#include "common/libs/sensors/sensors.h"
#include "common/libs/transport/channel_sharedfd.h"
#include "common/libs/utils/device_type.h"
#include "cuttlefish/host/commands/kernel_log_monitor/kernel_log_server.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include "host/commands/sensors_simulator/sensors_simulator.h"

namespace cuttlefish {
namespace sensors {

class SensorsHalProxy {
 public:
  SensorsHalProxy(SharedFD sensors_in_fd, SharedFD sensors_out_fd,
                  SharedFD kernel_events_fd,
                  SensorsSimulator& sensors_simulator, DeviceType device_type);

 private:
  std::thread req_responder_thread_;
  std::thread data_reporter_thread_;
  std::thread reboot_monitor_thread_;
  transport::SharedFdChannel channel_;
  SharedFD kernel_events_fd_;
  SensorsSimulator& sensors_simulator_;
  std::atomic<bool> hal_activated_ = false;
  std::atomic<bool> running_ = true;
};

}  // namespace sensors
}  // namespace cuttlefish