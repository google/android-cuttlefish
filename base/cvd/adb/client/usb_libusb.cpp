/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "usb_libusb.h"

#include "android-base/logging.h"

#include "adb_trace.h"
#include "client/detach.h"
#include "client/usb_libusb_inhouse_hotplug.h"
#include "usb.h"

using namespace std::chrono_literals;

using android::base::ScopedLockAssertion;

LibUsbConnection::LibUsbConnection(std::unique_ptr<LibUsbDevice> device)
    : device_(std::move(device)) {}

void LibUsbConnection::Init() {
    detached_ = attached_devices.ShouldStartDetached(const_cast<LibUsbConnection&>(*this));
    VLOG(USB) << "Device " << device_->GetSerial() << " created detached=" << detached_;
}

LibUsbConnection::~LibUsbConnection() {
    VLOG(USB) << "LibUsbConnection(" << Serial() << "): destructing";
    Stop();
}

void LibUsbConnection::OnError(const std::string& reason) {
    std::call_once(this->error_flag_, [this, reason]() {
        // Clears halt condition for endpoints when an error is encountered. This logic was moved
        // here from LibUsbDevice::ClaimInterface() where calling it as part of the open device
        // flow would cause some devices to enter a state where communication was broken. See issue
        // https://issuetracker.google.com/issues/404741058
        device_->ClearEndpoints();

        // When a Windows machine goes to sleep it powers off all its USB host controllers to save
        // energy. When the machine awakens, it powers them up which causes all the endpoints
        // to be closed (which generates a read/write failure leading to us Close()ing the device).
        // The USB device also briefly goes away and comes back with the exact same properties
        // (including address). This makes in-house hotplug miss device reconnection upon wakeup. To
        // solve that we remove ourselves from the set of known devices.
        libusb_inhouse_hotplug::report_error(*this);

        transport_->HandleError(reason);
    });
}

void LibUsbConnection::HandleStop(const std::string& reason) {
    // If we are detached, we should not report an error condition to the transport
    // layer. If a connection is detached it has merely been requested to stop transmitting and
    // release its resources.
    if (detached_) {
        VLOG(USB) << "Not reporting error '" << reason << "' because device " << transport_->serial
                  << " is detached";
    } else {
        OnError(reason);
    }
}

bool LibUsbConnection::Start() {
    VLOG(USB) << "LibUsbConnection::Start()";
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        VLOG(USB) << "LibUsbConnection(" << Serial() << "): already started";
    }

    if (!device_->Open()) {
        VLOG(USB) << "Unable to start " << Serial() << ": Failed to open device";
        return false;
    }

    StartReadThread();
    StartWriteThread();

    running_ = true;
    return true;
}

void LibUsbConnection::StartReadThread() {
    read_thread_ = std::thread([this]() {
        VLOG(USB) << Serial() << ": read thread spawning";
        while (true) {
            auto packet = std::make_unique<apacket>();
            if (!device_->Read(packet.get())) {
                PLOG(INFO) << Serial() << ": read failed";
                break;
            }
            transport_->HandleRead(std::move(packet));
        }
        HandleStop("read thread stopped");
    });
}

void LibUsbConnection::StartWriteThread() {
    write_thread_ = std::thread([this]() {
        VLOG(USB) << Serial() << ": write thread spawning";
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            ScopedLockAssertion assume_locked(mutex_);
            cv_write_.wait(lock, [this]() REQUIRES(mutex_) {
                return !this->running_ || !this->write_queue_.empty();
            });

            if (!this->running_) {
                break;
            }

            std::unique_ptr<apacket> packet = std::move(this->write_queue_.front());
            this->write_queue_.pop_front();
            lock.unlock();

            if (!this->device_->Write(packet.get())) {
                break;
            }
        }
        HandleStop("write thread stopped");
    });
}

bool LibUsbConnection::DoTlsHandshake(RSA* key, std::string* auth_key) {
    LOG(WARNING) << "TlsHandshake is not supported by libusb backend";
    return false;
}

void LibUsbConnection::Reset() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            LOG(INFO) << "LibUsbConnection(" << Serial() << "): not running";
            return;
        }
    }

    LOG(INFO) << "LibUsbConnection(" << Serial() << "): RESET";
    this->device_->Reset();
    Stop();
}

void LibUsbConnection::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_) {
            LOG(INFO) << "LibUsbConnection(" << Serial() << ") Stop: not running";
            return;
        }

        running_ = false;
    }

    LOG(INFO) << "LibUsbConnection(" << Serial() << "): stopping";

    this->device_->Close();
    this->cv_write_.notify_one();

    // Move the threads out into locals with the lock taken, and then unlock to let them exit.
    std::thread read_thread;
    std::thread write_thread;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        read_thread = std::move(read_thread_);
        write_thread = std::move(write_thread_);
    }

    read_thread.join();
    write_thread.join();

    HandleStop("stop requested");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        write_queue_.clear();
    }
}

bool LibUsbConnection::Write(std::unique_ptr<apacket> packet) {
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        write_queue_.emplace_back(std::move(packet));
    }

    cv_write_.notify_one();
    return true;
}

uint64_t LibUsbConnection::NegotiatedSpeedMbps() {
    return device_->NegotiatedSpeedMbps();
}

uint64_t LibUsbConnection::MaxSpeedMbps() {
    return device_->MaxSpeedMbps();
}

bool LibUsbConnection::SupportsDetach() const {
    return true;
}

bool LibUsbConnection::Attach(std::string*) {
    VLOG(USB) << "LibUsbConnection::Attach";

    if (!detached_) {
        VLOG(USB) << "Already attached";
        return true;
    }

    detached_ = false;
    return Start();
}

bool LibUsbConnection::Detach(std::string*) {
    VLOG(USB) << "LibUsbConnection::Detach";
    if (detached_) {
        VLOG(USB) << "Already detached";
        return true;
    }

    detached_ = true;
    Stop();
    return true;
}

bool LibUsbConnection::IsDetached() {
    return detached_;
}

uint64_t LibUsbConnection::GetSessionId() const {
    return device_->GetSessionId().id;
}
