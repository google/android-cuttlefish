/*
 * Copyright (C) 2021 The Android Open Source Project
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
#define LOG_TAG "VsockCameraServer"
#include "vsock_camera_server.h"
#include <log/log.h>

namespace android::hardware::camera::provider::V2_7::implementation {

using ::android::hardware::camera::device::V3_4::implementation::
    VsockCameraDevice;
namespace {

bool containsValidSettings(const VsockCameraDevice::Settings& settings) {
  return settings.width > 0 && settings.height > 0 && settings.frame_rate > 0.0;
}

bool readSettingsFromJson(VsockCameraDevice::Settings& settings,
                          const Json::Value& json) {
  VsockCameraDevice::Settings new_settings;
  new_settings.width = json["width"].asInt();
  new_settings.height = json["height"].asInt();
  new_settings.frame_rate = json["frame_rate"].asDouble();
  if (containsValidSettings(new_settings)) {
    settings = new_settings;
    return true;
  } else {
    return false;
  }
}

}  // namespace

VsockCameraServer::VsockCameraServer() {
  ALOGI("%s: Create server", __FUNCTION__);
  connection_ = std::make_shared<cuttlefish::VsockServerConnection>();
}

VsockCameraServer::~VsockCameraServer() {
  ALOGI("%s: Destroy server", __FUNCTION__);
  stop();
}

void VsockCameraServer::start(unsigned int port, unsigned int cid) {
  stop();
  is_running_ = true;
  server_thread_ = std::thread([this, port, cid] { serverLoop(port, cid); });
}

void VsockCameraServer::stop() {
  connection_->ServerShutdown();
  is_running_ = false;
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
}

void VsockCameraServer::setConnectedCallback(callback_t callback) {
  connected_callback_ = callback;
  std::lock_guard<std::mutex> lock(settings_mutex_);
  if (callback && connection_->IsConnected() &&
      containsValidSettings(settings_)) {
    callback(connection_, settings_);
  }
}

void VsockCameraServer::serverLoop(unsigned int port, unsigned int cid) {
  while (is_running_.load()) {
    ALOGI("%s: Accepting connections...", __FUNCTION__);
    if (connection_->Connect(port, cid)) {
      auto json_settings = connection_->ReadJsonMessage();
      VsockCameraDevice::Settings settings;
      if (readSettingsFromJson(settings, json_settings)) {
        std::lock_guard<std::mutex> lock(settings_mutex_);
        settings_ = settings;
        if (connected_callback_) {
          connected_callback_(connection_, settings);
        }
        ALOGI("%s: Client connected", __FUNCTION__);
      } else {
        ALOGE("%s: Could not read settings", __FUNCTION__);
      }
    } else {
      ALOGE("%s: Accepting connections failed", __FUNCTION__);
    }
  }
  ALOGI("%s: Exiting", __FUNCTION__);
}

}  // namespace android::hardware::camera::provider::V2_7::implementation
