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
#pragma once

#include <functional>
#include <memory>

#include <stdint.h>
#include <libusb/libusb.h>
#include "guest/usbforward/protocol.h"

namespace usb_forward {

// TransportRequest represents a libusb asynchronous transport request.
// This class encapsulates everything that is necessary to complete
// transfer.
class TransportRequest final {
 public:
  // CallbackType describes what kind of function can receive call when this
  // asynchronous call is complete.
  // Parameters passed to callback, in order:
  // - success indicator (true = success),
  // - buffer with data (in or out),
  // - actual length transferred.
  using CallbackType = std::function<void(bool, const uint8_t*, int32_t)>;

  TransportRequest(libusb_device_handle* device, CallbackType callback,
                   const ControlTransfer& transfer);
  TransportRequest(libusb_device_handle* device, CallbackType callback,
                   const DataTransfer& transfer);
  ~TransportRequest() = default;

  uint8_t* Buffer();

  // Submit sends an asynchronous data exchange requests.
  // Returns true only if operation was successful. At this point
  // ownership of this structure is passed to libusb and user
  // must not release the underlying structure.
  bool Submit();

  // Executes corresponding callback with execution results.
  // This is a static call to ensure that the callback being invoked
  // can dispose of this instance.
  static void OnTransferComplete(libusb_transfer* req);

 private:
  libusb_device_handle* handle_;
  CallbackType callback_;
  bool is_control_;
  std::unique_ptr<libusb_transfer, void(*)(libusb_transfer*)> transfer_;
  std::unique_ptr<uint8_t[]> buffer_;

  TransportRequest(const TransportRequest& other) = delete;
  TransportRequest& operator=(const TransportRequest& other) = delete;
};

}  // namespace usb_forward