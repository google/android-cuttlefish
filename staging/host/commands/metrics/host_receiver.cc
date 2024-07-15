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

#include "host/commands/metrics/events.h"
#include "host/commands/metrics/host_receiver.h"
#include "host/commands/metrics/metrics_configs.h"
#include "host/commands/metrics/metrics_defs.h"
#include "host/commands/metrics/proto/cf_metrics_protos.h"
#include "host/libs/metrics/metrics_receiver.h"
#include "host/libs/msg_queue/msg_queue.h"

namespace cuttlefish {

MetricsHostReceiver::MetricsHostReceiver(bool is_metrics_enabled)
    : is_metrics_enabled_(is_metrics_enabled) {}

MetricsHostReceiver::~MetricsHostReceiver() {}

void MetricsHostReceiver::ServerLoop() {
  auto msg_queue = SysVMessageQueue::Create(metrics_queue_name_);
  if (msg_queue == NULL) {
    LOG(FATAL) << "create: failed to create" << metrics_queue_name_;
  }

  struct msg_buffer msg = {0, {0}};
  while (true) {
    int rc = msg_queue->Receive(&msg, MAX_MSG_SIZE, 1, true);
    if (rc == -1) {
      LOG(FATAL) << "receive: failed to receive any messages";
    }

    std::string text(msg.mesg_text);
    LOG(INFO) << "Metrics host received: " << text;

    // Process the received message
    ProcessMessage(text);

    sleep(1);
  }
}

void MetricsHostReceiver::Join() { thread_.join(); }

bool MetricsHostReceiver::Initialize(const std::string& metrics_queue_name) {
  metrics_queue_name_ = metrics_queue_name;
  if (!is_metrics_enabled_) {
    LOG(ERROR) << "init: metrics not enabled";
    return false;
  }

  // Start the server loop in a separate thread
  thread_ = std::thread(&MetricsHostReceiver::ServerLoop, this);
  return true;
}

void MetricsHostReceiver::ProcessMessage(const std::string& text) {
  auto hostDev = CuttlefishLogEvent::CUTTLEFISH_DEVICE_TYPE_HOST;

  int rc = MetricsExitCodes::kSuccess;

  if (text == "VMStart") {
    rc = Clearcut::SendVMStart(hostDev);
  } else if (text == "VMStop") {
    rc = Clearcut::SendVMStop(hostDev);
  } else if (text == "DeviceBoot") {
    rc = Clearcut::SendDeviceBoot(hostDev);
  } else if (text == "LockScreen") {
    rc = Clearcut::SendLockScreen(hostDev);
  } else {
    LOG(ERROR) << "Message not recognized: " << text;
    rc = MetricsExitCodes::kMetricsError;
  }

  if (rc != MetricsExitCodes::kSuccess) {
    LOG(ERROR) << "Message failed to send to ClearCut: " << text;
  }
}

}  // namespace cuttlefish
