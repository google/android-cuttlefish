/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "sysdeps.h"
#include "types.h"

#include "usb_libusb_device.h"

struct LibUsbConnection : Connection {
    explicit LibUsbConnection(std::unique_ptr<LibUsbDevice> device);
    ~LibUsbConnection() override;

    void Init();

    bool Write(std::unique_ptr<apacket> packet) override;

    // Start transmitting. Start the write thread to consume from the
    // write queue, Start the read thread to retrieve packets and send
    // them to the transport layer.
    bool Start() override;

    // Stop both read and write threads.
    void Stop() override;

    // Not supported
    bool DoTlsHandshake(RSA* key, std::string* auth_key) override;

    // Reset the device. This will cause transmission to stop.
    void Reset() override;

    uint64_t NegotiatedSpeedMbps() override;
    uint64_t MaxSpeedMbps() override;

    bool SupportsDetach() const override;

    // Stop transmitting and release transmission resources but don't report
    // an error to the transport layer. Detaching allows another ADB server
    // running on the same host to take over a device.
    bool Attach(std::string* error) override;

    // Opposite of Attach, re-acquire transmission resources and start
    // transmitting.
    bool Detach(std::string* error) override;

    bool IsDetached();

    // Report an error condition to the upper layer. This will result
    // in transport calling Stop() and this connection be destroyed
    // on the fdevent thread.
    void OnError(const std::string& error);

    uint64_t GetSessionId() const;

  private:
    std::atomic<bool> detached_ = false;

    void HandleStop(const std::string& reason);

    void StartReadThread() REQUIRES(mutex_);
    void StartWriteThread() REQUIRES(mutex_);
    bool running_ GUARDED_BY(mutex_) = false;

    std::unique_ptr<LibUsbDevice> device_;
    std::thread read_thread_ GUARDED_BY(mutex_);
    std::thread write_thread_ GUARDED_BY(mutex_);

    // To improve throughput, we store apacket in a queue upon Write. This
    // queue is consumed by the write thread.
    std::deque<std::unique_ptr<apacket>> write_queue_ GUARDED_BY(mutex_);
    std::mutex mutex_;

    // Unlock the Write thread when we need to stop or when there are packets
    // to Write.
    std::condition_variable cv_write_;

    std::once_flag error_flag_;
};