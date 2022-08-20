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

#include <common/libs/fs/shared_select.h>
#include <host/commands/kernel_log_monitor/kernel_log_server.h>
#include <host/commands/kernel_log_monitor/utils.h>
#include <host/libs/config/cuttlefish_config.h>

using namespace android;

namespace cuttlefish {

KernelLogEventsHandler::KernelLogEventsHandler(
    SharedFD kernel_log_fd)
    : kernel_log_fd_(kernel_log_fd),
      eventfd_(SharedFD::Event()),
      running_(true),
      read_thread_([this]() { ReadLoop(); }) {}

KernelLogEventsHandler::~KernelLogEventsHandler() {
  running_ = false;
  eventfd_->EventfdWrite(1);
  read_thread_.join();
}

void KernelLogEventsHandler::ReadLoop() {
  CHECK(eventfd_->IsOpen()) << "Failed to create event fd: "
                           << eventfd_->StrError();
  while (running_) {
    SharedFDSet read_set;
    read_set.Set(eventfd_);
    read_set.Set(kernel_log_fd_);
    auto select_ret = Select(&read_set, nullptr, nullptr, nullptr);
    if (select_ret < 0) {
      LOG(ERROR) << "Error on select call";
      break;
    }
    if (read_set.IsSet(eventfd_)) {
      eventfd_t evt;
      (void)eventfd_->EventfdRead(&evt);
      if (!running_) {
        // There won't be anyone listening for kernel log events if the thread
        // was asked to stop, so break out of the loop without reading.
        break;
      }
    }
    if (read_set.IsSet(kernel_log_fd_)) {
      std::optional<monitor::ReadEventResult> read_result =
          monitor::ReadEvent(kernel_log_fd_);
      if (!read_result) {
        LOG(ERROR) << "Failed to read kernel log event: "
                   << kernel_log_fd_->StrError();
        break;
      }

      if (read_result->event == monitor::Event::BootStarted) {
        Json::Value message;
        message["event"] = kBootStartedMessage;
        DeliverEvent(message);
      }
      if (read_result->event == monitor::Event::BootCompleted) {
        Json::Value message;
        message["event"] = kBootCompletedMessage;
        DeliverEvent(message);
      }
      if (read_result->event == monitor::Event::ScreenChanged) {
        Json::Value message;
        message["event"] = kScreenChangedMessage;
        message["metadata"] = read_result->metadata;
        DeliverEvent(message);
      }
      if (read_result->event == monitor::Event::DisplayPowerModeChanged) {
        Json::Value message;
        message["event"] = kDisplayPowerModeChangedMessage;
        message["metadata"] = read_result->metadata;
        DeliverEvent(message);
      }
    }
  }
}

int KernelLogEventsHandler::AddSubscriber(
    std::function<void(const Json::Value&)> subscriber) {
  std::lock_guard<std::mutex> lock(subscribers_mtx_);
  for (const auto& event : last_events_) {
    // Deliver the last event of each type to the new subscriber so that it can
    // show the correct state.
    subscriber(event);
  }
  subscribers_[++last_subscriber_id_] = subscriber;
  return last_subscriber_id_;
}

void KernelLogEventsHandler::Unsubscribe(int subscriber_id) {
  std::lock_guard<std::mutex> lock(subscribers_mtx_);
  subscribers_.erase(subscriber_id);
}

void KernelLogEventsHandler::DeliverEvent(const Json::Value& event) {
  std::lock_guard<std::mutex> lock(subscribers_mtx_);
  // event["event"] is actually the type of the event.
  // This would be more efficient with a set, but a list maintains the order in
  // which events arrived. And for just a handful of elements the list can
  // actually perform better.
  for (auto it = last_events_.begin();
       it != last_events_.end(); it++) {
    if ((*it)["event"].asString() == event["event"].asString()) {
      last_events_.erase(it);
      break;
    }
  }
  last_events_.push_back(event);
  for (const auto& entry : subscribers_) {
    entry.second(event);
  }
}

}  // namespace cuttlefish
