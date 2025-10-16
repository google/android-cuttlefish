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
#include <multihal_sensors.h>
#include <multihal_sensors_transport.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/transport/channel_sharedfd.h"

using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;

namespace {

class VconsoleSensorsTransport : public goldfish::SensorsTransport {
 public:
  VconsoleSensorsTransport(cuttlefish::SharedFD control_fd,
                           cuttlefish::SharedFD data_fd)
      : control_fd_(std::move(control_fd)),
        data_fd_(std::move(data_fd)),
        pure_control_fd_(control_fd_->UNMANAGED_Dup()),
        pure_data_fd_(data_fd_->UNMANAGED_Dup()),
        control_channel_(control_fd_, control_fd_),
        data_channel_(data_fd_, data_fd_) {}

  ~VconsoleSensorsTransport() override {
    close(pure_control_fd_);
    close(pure_data_fd_);
  }

  int Send(goldfish::SensorsMessageType type, const void* msg,
           int size) override {
    auto channel = GetChannel(type);

    auto message_result = cuttlefish::transport::CreateMessage(0, size);
    if (!message_result.ok()) {
      LOG(ERROR) << "Failed to allocate sensors message with size: " << size
                 << " bytes. "
                 << "Error message: " << message_result.error().Message();
      return -1;
    }

    auto message = std::move(message_result.value());
    std::memcpy(message->payload, msg, size);

    auto send_result = channel.SendRequest(*message);
    if (!send_result.ok()) {
      LOG(ERROR) << "Failed to send sensors message with size: " << size
                 << " bytes. "
                 << "Error message: " << send_result.error().Message();
      return -1;
    }

    return size;
  }

  int Receive(goldfish::SensorsMessageType type, void* msg,
              int maxsize) override {
    auto channel = GetChannel(type);

    auto message_result = channel.ReceiveMessage();
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

  bool Ok() const override {
    return control_fd_->IsOpen() && data_fd_->IsOpen();
  }

  int Fd(goldfish::SensorsMessageType type) const override {
    return GetPureFd(type);
  }

  const char* Name() const override { return "vconsole_channel"; }

 private:
  cuttlefish::transport::SharedFdChannel& GetChannel(
      goldfish::SensorsMessageType type) {
    switch (type) {
      case goldfish::SensorsMessageType::CONTROL:
        return control_channel_;
      case goldfish::SensorsMessageType::DATA:
        return data_channel_;
    }
  }

  int GetPureFd(goldfish::SensorsMessageType type) const {
    switch (type) {
      case goldfish::SensorsMessageType::CONTROL:
        return pure_control_fd_;
      case goldfish::SensorsMessageType::DATA:
        return pure_data_fd_;
    }
  }

  cuttlefish::SharedFD control_fd_;
  cuttlefish::SharedFD data_fd_;

  // Store pure dup of control_fd and data_fd_ to return them from Fd() method
  // which supposed to return pure fd used for receive/send sensors data.
  int pure_control_fd_;
  int pure_data_fd_;

  cuttlefish::transport::SharedFdChannel control_channel_;
  cuttlefish::transport::SharedFdChannel data_channel_;
};

}  // namespace

inline constexpr const char kSensorsControlPath[] = "/dev/hvc18";
inline constexpr const char kSensorsDataPath[] = "/dev/hvc19";

extern "C" ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t* version) {
  const auto control_fd =
      cuttlefish::SharedFD::Open(kSensorsControlPath, O_RDWR);
  if (!control_fd->IsOpen()) {
    LOG(FATAL) << "Could not connect to sensors control: "
               << control_fd->StrError();
  }
  if (control_fd->SetTerminalRaw() < 0) {
    LOG(FATAL) << "Could not make " << kSensorsControlPath
               << " a raw terminal: " << control_fd->StrError();
  }

  const auto data_fd = cuttlefish::SharedFD::Open(kSensorsDataPath, O_RDWR);
  if (!data_fd->IsOpen()) {
    LOG(FATAL) << "Could not connect to sensors data: " << data_fd->StrError();
  }
  if (data_fd->SetTerminalRaw() < 0) {
    LOG(FATAL) << "Could not make " << kSensorsDataPath
               << " a raw terminal: " << data_fd->StrError();
  }

  // Leaking the memory intentionally to make sure this object is available
  // for other threads after main thread is terminated:
  // https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables
  // go/totw/110#destruction
  static goldfish::MultihalSensors* impl =
      new goldfish::MultihalSensors([control_fd, data_fd]() {
        return std::make_unique<VconsoleSensorsTransport>(control_fd, data_fd);
      });

  *version = SUB_HAL_2_1_VERSION;
  return impl;
}
