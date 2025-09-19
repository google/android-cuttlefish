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

#include "cuttlefish/host/commands/metrics/proto/cf_log.pb.h"
#include "cuttlefish/host/commands/metrics/proto/cf_metrics_event.pb.h"

namespace cuttlefish {

class Clearcut {
 private:
  static int SendEvent(logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent::DeviceType device_type,
                       logs::proto::wireless::android::cuttlefish::events::MetricsEvent::EventType event_type);

 public:
  Clearcut() = default;
  ~Clearcut() = default;
  static int SendVMStart(logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent::DeviceType device_type);
  static int SendVMStop(logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent::DeviceType device_type);
  static int SendDeviceBoot(logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent::DeviceType device_type);
  static int SendLockScreen(logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent::DeviceType device_type);
};

}  // namespace cuttlefish
