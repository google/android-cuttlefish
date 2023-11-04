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
  ALOGI("%s: Create server", __PRETTY_FUNCTION__);
}

VsockCameraServer::~VsockCameraServer() {
  ALOGI("%s: Destroy server", __PRETTY_FUNCTION__);
  stop();
}

void VsockCameraServer::start(unsigned int port, unsigned int cid) {
  stop();
  is_running_ = true;
  server_thread_ = std::thread([this, port, cid] { serverLoop(port, cid); });
}

void VsockCameraServer::stop() {
  server_.Stop();
  connection_->Disconnect();
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
    ALOGI("%s: Accepting connections...", __PRETTY_FUNCTION__);
    /* vhost_user_vsock: nullopt because it's guest */
    if (!server_.IsRunning() && !server_.Start(port, cid, std::nullopt).ok()) {
      ALOGE("%s: Failed to start server", __PRETTY_FUNCTION__);
      continue;
    }
    auto connect_result = server_.AcceptConnection();
    if (!connect_result.ok()) {
      ALOGE("%s: Accepting connections failed", __PRETTY_FUNCTION__);
      continue;
    }
    connection_ = std::move(*connect_result);
    auto json_settings_result = connection_->ReadJsonMessage();
    if (json_settings_result.ok()) {
      ALOGE("%s: Could not read settings", __PRETTY_FUNCTION__);
      continue;
    }
    auto json_settings = *json_settings_result;
    VsockCameraDevice::Settings settings;
    if (readSettingsFromJson(settings, json_settings)) {
      std::lock_guard<std::mutex> lock(settings_mutex_);
      settings_ = settings;
      if (connected_callback_) {
        connected_callback_(connection_, settings);
      }
      ALOGI("%s: Client connected", __PRETTY_FUNCTION__);
    } else {
      ALOGE("%s: Could not read settings", __PRETTY_FUNCTION__);
    }
  }
  ALOGI("%s: Exiting", __PRETTY_FUNCTION__);
}

}  // namespace android::hardware::camera::provider::V2_7::implementation
