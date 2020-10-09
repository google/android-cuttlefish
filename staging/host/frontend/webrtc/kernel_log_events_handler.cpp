/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "host/frontend/webrtc/kernel_log_events_handler.h"

#include <android-base/logging.h>

#include <host/commands/kernel_log_monitor/kernel_log_server.h>
#include <host/commands/kernel_log_monitor/utils.h>
#include <host/libs/config/cuttlefish_config.h>

using namespace android;

namespace cuttlefish {
namespace webrtc_streaming {

KernelLogEventsHandler::KernelLogEventsHandler(
    SharedFD kernel_log_fd,
    std::function<void(const Json::Value&)> send_to_client)
    : send_to_client_(send_to_client),
      kernel_log_fd_(kernel_log_fd),
      read_thread_([this]() { ReadLoop(); }) {}

KernelLogEventsHandler::~KernelLogEventsHandler() {
        (*kernel_log_fd_).Close();
        read_thread_.join();
}

void KernelLogEventsHandler::ReadLoop() {
  while (1) {
    std::optional<monitor::ReadEventResult> read_result =
        monitor::ReadEvent(kernel_log_fd_);
    if (!read_result) {
      LOG(ERROR) << "Failed to read kernel log event: " << kernel_log_fd_->StrError();
      break;
    }

    if (read_result->event == monitor::Event::BootStarted) {
      Json::Value message;
      message["event"] = kBootStartedMessage;
      send_to_client_(message);
    }
    if (read_result->event == monitor::Event::ScreenChanged) {
      Json::Value message;
      message["event"] = kScreenChangedMessage;
      message["metadata"] = read_result->metadata;
      send_to_client_(message);
    }
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
