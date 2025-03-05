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
  VconsoleSensorsTransport(cuttlefish::SharedFD fd)
      : console_sensors_fd_(std::move(fd)),
        pure_sensors_fd_(console_sensors_fd_->UNMANAGED_Dup()),
        sensors_channel_(console_sensors_fd_, console_sensors_fd_) {
    // When the guest reboots, sensors_simulator on the host would continue
    // writing sensor data to FIFO till BootloaderLoaded kernel event fires. The
    // residual sensor data in sensor FIFO could interfere with sensor HAL init
    // process. Hence, to be safe, let's clean up the FIFO when instantiating
    // the transport.
    if (Drain() < 0) {
      LOG(FATAL) << "Failed to drain FIFO: " << console_sensors_fd_->StrError();
    }
  }

  ~VconsoleSensorsTransport() override { close(pure_sensors_fd_); }

  int Send(const void* msg, int size) override {
    auto message_result = cuttlefish::transport::CreateMessage(0, size);
    if (!message_result.ok()) {
      LOG(ERROR) << "Failed to allocate sensors message with size: " << size
                 << " bytes. "
                 << "Error message: " << message_result.error().Message();
      return -1;
    }

    auto message = std::move(message_result.value());
    std::memcpy(message->payload, msg, size);

    auto send_result = sensors_channel_.SendRequest(*message);
    if (!send_result.ok()) {
      LOG(ERROR) << "Failed to send sensors message with size: " << size
                 << " bytes. "
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

  int Drain() {
    int original_flags = console_sensors_fd_->Fcntl(F_GETFL, 0);
    if (original_flags == -1) {
      LOG(ERROR) << "Failed to get current file descriptor flags.";
      return -1;
    }

    if (console_sensors_fd_->Fcntl(F_SETFL, original_flags | O_NONBLOCK) ==
        -1) {
      LOG(ERROR) << "Failed to set O_NONBLOCK.";
      return -1;
    }

    std::string data;
    if (ReadAll(console_sensors_fd_, &data) < 0 &&
        console_sensors_fd_->GetErrno() != EAGAIN) {
      LOG(ERROR) << "Failed to read the file.";
      return -1;
    }

    if (console_sensors_fd_->Fcntl(F_SETFL, original_flags) == -1) {
      LOG(ERROR) << "Failed to restore to original file descriptor flags.";
      return -1;
    }

    return 0;
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

inline constexpr const char kSensorsConsolePath[] = "/dev/hvc13";

extern "C" ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t* version) {
  // Leaking the memory intentionally to make sure this object is available
  // for other threads after main thread is terminated:
  // https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables
  // go/totw/110#destruction
  static goldfish::MultihalSensors* impl = new goldfish::MultihalSensors([]() {
    const auto fd = cuttlefish::SharedFD::Open(kSensorsConsolePath, O_RDWR);
    if (!fd->IsOpen()) {
      LOG(FATAL) << "Could not connect to sensors: " << fd->StrError();
    }
    if (fd->SetTerminalRaw() < 0) {
      LOG(FATAL) << "Could not make " << kSensorsConsolePath
                 << " a raw terminal: " << fd->StrError();
    }
    return std::make_unique<VconsoleSensorsTransport>(fd);
  });

  *version = SUB_HAL_2_1_VERSION;
  return impl;
}
