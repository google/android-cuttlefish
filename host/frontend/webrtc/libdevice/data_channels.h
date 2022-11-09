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

#include <functional>
#include <memory>

#include <api/data_channel_interface.h>

#include "host/frontend/webrtc/libdevice/connection_observer.h"

namespace cuttlefish {
namespace webrtc_streaming {

constexpr auto kControlChannelLabel = "device-control";

class DataChannelHandler;

// Groups all data channel handlers.
// Each handler is an implementation of the DataChannelHandler abstract class
// providing custom message handlers and calling the appropriate methods on the
// connection observer.
class DataChannelHandlers {
 public:
  DataChannelHandlers(std::shared_ptr<ConnectionObserver> observer);
  ~DataChannelHandlers();

  void OnDataChannelOpen(
      rtc::scoped_refptr<webrtc::DataChannelInterface> channel);

 private:
  std::unique_ptr<DataChannelHandler> input_;
  std::unique_ptr<DataChannelHandler> control_;
  std::unique_ptr<DataChannelHandler> adb_;
  std::unique_ptr<DataChannelHandler> bluetooth_;
  std::unique_ptr<DataChannelHandler> camera_;
  std::unique_ptr<DataChannelHandler> location_;
  std::unique_ptr<DataChannelHandler> kml_location_;
  std::unique_ptr<DataChannelHandler> gpx_location_;
  std::vector<std::unique_ptr<DataChannelHandler>> unknown_channels_;

  std::shared_ptr<ConnectionObserver> observer_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
