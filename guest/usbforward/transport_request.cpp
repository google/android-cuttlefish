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

#include "guest/usbforward/transport_request.h"

#include <cutils/log.h>

namespace usb_forward {

TransportRequest::TransportRequest(libusb_device_handle* handle,
                                   CallbackType callback,
                                   const ControlTransfer& transfer)
    : handle_(handle), callback_(std::move(callback)), is_control_(true) {
  // NOTE: libusb places setup structure as part of user data!
  buffer_.reset(new uint8_t[transfer.length + LIBUSB_CONTROL_SETUP_SIZE]);

  // NOTE: libusb places a structure of size LIBUSB_CONTROL_SETUP_SIZE directly
  // in the data buffer.
  libusb_fill_control_setup(buffer_.get(), transfer.type, transfer.cmd,
                            transfer.value, transfer.index, transfer.length);

  // NOTE: despite libusb requires user to allocate buffer large enough to
  // accommodate SETUP structure and actual data, it requires user to provide
  // only data length here, while setup length is added internally.
  if (handle_) {
    libusb_fill_control_transfer(&transfer_, handle_, buffer_.get(),
                                 OnTransferComplete, this, transfer.timeout);
  }
}

TransportRequest::TransportRequest(libusb_device_handle* handle,
                                   CallbackType callback,
                                   const DataTransfer& transfer)
    : handle_(handle), callback_(std::move(callback)), is_control_(false) {
  buffer_.reset(new uint8_t[transfer.length]);
  if (handle_) {
    libusb_fill_bulk_transfer(&transfer_, handle_,
                              transfer.endpoint_id | (transfer.is_host_to_device
                                                          ? LIBUSB_ENDPOINT_OUT
                                                          : LIBUSB_ENDPOINT_IN),
                              buffer_.get(), transfer.length,
                              OnTransferComplete, this, transfer.timeout);
  }
}

uint8_t* TransportRequest::Buffer() {
  if (is_control_) {
    return &buffer_[LIBUSB_CONTROL_SETUP_SIZE];
  } else {
    return buffer_.get();
  }
}

bool TransportRequest::Submit() {
  if (handle_) {
    auto err = libusb_submit_transfer(&transfer_);
    if (err != 0) {
      ALOGE("libusb transfer failed: %d", err);
    }
    return err == 0;
  } else {
    ALOGE("Initiated transfer, but device not opened.");
    return false;
  }
}

void TransportRequest::OnTransferComplete(libusb_transfer* req) {
  auto treq = static_cast<TransportRequest*>(req->user_data);
  treq->callback_(req->status == 0, treq->Buffer(), req->actual_length);
}

}  // namespace usb_forward