/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <log/log.h>
#include <multihal_sensors_transport.h>
#include <multihal_sensors.h>

#include "common/libs/transport/channel_sharedfd.h"

using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;

namespace {

class VconsoleSensorsTransport : public goldfish::SensorsTransport {
 public:
  VconsoleSensorsTransport(const char* path)
      : console_sensors_fd_(cuttlefish::SharedFD::Open(path, O_RDWR)),
        pure_sensors_fd_(console_sensors_fd_->UNMANAGED_Dup()),
        sensors_channel_(console_sensors_fd_, console_sensors_fd_) {}

  ~VconsoleSensorsTransport() override { close(pure_sensors_fd_); }

  int Send(const void* msg, int size) override {
    auto message_result = cuttlefish::transport::CreateMessage(0, size);
    if (!message_result.ok()) {
      LOG(ERROR) << "Failed to allocate sensors message with size: " << size << " bytes. "
                 << "Error message: " << message_result.error().Message();
      return -1;
    }

    auto message = std::move(message_result.value());
    std::memcpy(message->payload, msg, size);

    auto send_result = sensors_channel_.SendRequest(*message);
    if (!send_result.ok()) {
      LOG(ERROR) << "Failed to send sensors message with size: " << size << " bytes. "
                 << "Error message: " << send_result.error().Message();
      return -1;
    }

    return size;
  }

  int Receive(void* msg, int maxsize) override {
    auto message_result = sensors_channel_.ReceiveMessage();
    if (!message_result.ok()) {
      LOG(ERROR) << "Failed to receive sensors message. "
                 << "Error message: " << message_result.error().Message();
      return -1;
    }

    auto message = std::move(message_result.value());
    if (message->payload_size > maxsize) {
      LOG(ERROR) << "Received sensors message size is " << message->payload_size
                 << " maximum supported size is " << maxsize;
      return -1;
    }

    std::memcpy(msg, message->payload, message->payload_size);

    return message->payload_size;
  }

  bool Ok() const override { return console_sensors_fd_->IsOpen(); }

  int Fd() const override { return pure_sensors_fd_; }

  const char* Name() const override { return "vconsole_channel"; }

 private:
  cuttlefish::SharedFD console_sensors_fd_;
  // Store pure dup of console_sensors_fd_ to return it from
  // Fd() method which supposed to return pure fd used for
  // receive/send sensors data.
  int pure_sensors_fd_;
  cuttlefish::transport::SharedFdChannel sensors_channel_;
};

}  // namespace

extern "C" ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t* version) {
  // Leaking the memory intentionally to make sure this object is available
  // for other threads after main thread is terminated:
  // https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables
  // go/totw/110#destruction
  static goldfish::MultihalSensors* impl = new goldfish::MultihalSensors([]() {
    return std::make_unique<VconsoleSensorsTransport>("/dev/hvc13");
  });

  *version = SUB_HAL_2_1_VERSION;
  return impl;
}
