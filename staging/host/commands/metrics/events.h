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

#include "host/commands/metrics/proto/cf_metrics_proto.h"

namespace cuttlefish {

class Clearcut {
 private:
  static int SendEvent(cuttlefish::CuttlefishLogEvent::DeviceType device_type,
                       cuttlefish::MetricsEvent::EventType event_type);

 public:
  Clearcut();
  ~Clearcut();
  static int SendVMStart(
      cuttlefish::CuttlefishLogEvent::DeviceType device_type);
  static int SendVMStop(cuttlefish::CuttlefishLogEvent::DeviceType device_type);
  static int SendDeviceBoot(
      cuttlefish::CuttlefishLogEvent::DeviceType device_type);
  static int SendLockScreen(
      cuttlefish::CuttlefishLogEvent::DeviceType device_type);
};

}  // namespace cuttlefish
