/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "vsoc_input_service.h"

#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/virtio_input.h>

#include <thread>

#include <gflags/gflags.h>
#include "log/log.h"
#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/device_config/device_config.h"

using cuttlefish::input_events::InputEvent;
using cuttlefish_input_service::VirtualDeviceBase;
using cuttlefish_input_service::VirtualKeyboard;
using cuttlefish_input_service::VirtualPowerButton;
using cuttlefish_input_service::VirtualTouchScreen;
using cuttlefish_input_service::VSoCInputService;

DEFINE_uint32(keyboard_port, 0, "keyboard vsock port");
DEFINE_uint32(touch_port, 0, "keyboard vsock port");

namespace {

void EventLoop(std::shared_ptr<VirtualDeviceBase> device,
               std::function<InputEvent()> next_event) {
  while (1) {
    InputEvent event = next_event();
    device->EmitEvent(event.type, event.code, event.value);
  }
}

}  // namespace

bool VSoCInputService::SetUpDevices() {
  virtual_power_button_.reset(new VirtualPowerButton());
  if (!virtual_power_button_->SetUp()) {
    return false;
  }
  virtual_keyboard_.reset(new VirtualKeyboard());
  if (!virtual_keyboard_->SetUp()) {
    return false;
  }

  auto config = cuttlefish::DeviceConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to open device config";
    return false;
  }

  virtual_touchscreen_.reset(
      new VirtualTouchScreen(config->screen_x_res(), config->screen_y_res()));
  if (!virtual_touchscreen_->SetUp()) {
    return false;
  }

  return true;
}

bool VSoCInputService::ProcessEvents() {
  cuttlefish::SharedFD keyboard_fd;
  cuttlefish::SharedFD touch_fd;

  LOG(INFO) << "Connecting to the keyboard at " << FLAGS_keyboard_port;
  if (FLAGS_keyboard_port) {
    keyboard_fd = cuttlefish::SharedFD::VsockClient(2, FLAGS_keyboard_port, SOCK_STREAM);
    if (!keyboard_fd->IsOpen()) {
      LOG(ERROR) << "Could not connect to the keyboard at vsock:2:" << FLAGS_keyboard_port;
    }
    LOG(INFO) << "Connected to keyboard";
  }
  LOG(INFO) << "Connecting to the touchscreen at " << FLAGS_keyboard_port;
  if (FLAGS_touch_port) {
    touch_fd = cuttlefish::SharedFD::VsockClient(2, FLAGS_touch_port, SOCK_STREAM);
    if (!touch_fd->IsOpen()) {
      LOG(ERROR) << "Could not connect to the touch at vsock:2:" << FLAGS_touch_port;
    }
    LOG(INFO) << "Connected to touch";
  }

  // Start device threads
  std::thread screen_thread([this, touch_fd]() {
    EventLoop(virtual_touchscreen_, [touch_fd]() {
      struct virtio_input_event event;
      if (touch_fd->Read(&event, sizeof(event)) != sizeof(event)) {
        LOG(FATAL) << "Could not read touch event: " << touch_fd->StrError();
      }
      return InputEvent {
        .type = event.type,
        .code = event.code,
        .value = event.value,
      };
    });
  });
  std::thread keyboard_thread([this, keyboard_fd]() {
    EventLoop(virtual_keyboard_, [keyboard_fd]() {
      struct virtio_input_event event;
      if (keyboard_fd->Read(&event, sizeof(event)) != sizeof(event)) {
        LOG(FATAL) << "Could not read keyboard event: " << keyboard_fd->StrError();
      }
      return InputEvent {
        .type = event.type,
        .code = event.code,
        .value = event.value,
      };
    });
  });

  screen_thread.join();
  keyboard_thread.join();

  // Should never return
  return false;
}
