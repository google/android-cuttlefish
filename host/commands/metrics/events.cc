//
// Copyright (C) 2022 The Android Open Source Project
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

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <gflags/gflags.h>
#include <string>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/metrics/events.h"
#include "host/commands/metrics/metrics_defs.h"
#include "host/commands/metrics/proto/cf_metrics_proto.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"

using cuttlefish::MetricsExitCodes;

namespace cuttlefish {

Clearcut::Clearcut() {}

Clearcut::~Clearcut() {}

int Clearcut::SendEvent(cuttlefish::CuttlefishLogEvent::DeviceType device_type,
                        cuttlefish::MetricsEvent::EventType event_type) {
  LOG(INFO) << "Send Event Stub: " << device_type << ", " << event_type;
  return kSuccess;
}

int Clearcut::SendVMStart(cuttlefish::CuttlefishLogEvent::DeviceType device) {
  return SendEvent(
      device, cuttlefish::MetricsEvent::CUTTLEFISH_EVENT_TYPE_VM_INSTANTIATION);
}

int Clearcut::SendVMStop(cuttlefish::CuttlefishLogEvent::DeviceType device) {
  return SendEvent(device,
                   cuttlefish::MetricsEvent::CUTTLEFISH_EVENT_TYPE_VM_STOP);
}

int Clearcut::SendDeviceBoot(
    cuttlefish::CuttlefishLogEvent::DeviceType device) {
  return SendEvent(device,
                   cuttlefish::MetricsEvent::CUTTLEFISH_EVENT_TYPE_DEVICE_BOOT);
}

int Clearcut::SendLockScreen(
    cuttlefish::CuttlefishLogEvent::DeviceType device) {
  return SendEvent(
      device,
      cuttlefish::MetricsEvent::CUTTLEFISH_EVENT_TYPE_LOCK_SCREEN_AVAILABLE);
}

}  // namespace cuttlefish
