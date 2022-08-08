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

#include "metrics_receiver.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <fstream>
#include <iostream>
#include <memory>

#include "common/libs/utils/tee_logging.h"
#include "host/commands/metrics/metrics_defs.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/msg_queue/msg_queue.h"

using cuttlefish::MetricsExitCodes;

namespace cuttlefish {

MetricsReceiver::MetricsReceiver() {}

MetricsReceiver::~MetricsReceiver() {}

void MetricsReceiver::SendHelper(const std::string &message) {
  auto msg_queue = SysVMessageQueue::Create("cuttlefish_ipc", 'a', false);
  if (msg_queue == NULL) {
    LOG(FATAL) << "Create: failed to create cuttlefish_ipc";
  }

  struct msg_buffer msg;
  msg.mesg_type = 1;
  strcpy(msg.mesg_text, message.c_str());
  int rc = msg_queue->Send(&msg, message.length() + 1, true);
  if (rc == -1) {
    LOG(FATAL) << "Send: failed to send message to msg_queue";
  }
}

void MetricsReceiver::LogMetricsVMStart() { SendHelper("VMStart"); }

void MetricsReceiver::LogMetricsVMStop() { SendHelper("VMStop"); }

void MetricsReceiver::LogMetricsDeviceBoot() { SendHelper("DeviceBoot"); }

void MetricsReceiver::LogMetricsLockScreen() { SendHelper("LockScreen"); }
}  // namespace cuttlefish
