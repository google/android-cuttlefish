/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <aidl/android/hardware/secure_element/BnSecureElement.h>
#include <aidl/android/hardware/secure_element/ISecureElementCallback.h>

#include "common/libs/transport/channel_sharedfd.h"

namespace aidl::android::hardware::secure_element {
using cuttlefish::Result;
using cuttlefish::transport::ManagedMessage;
using cuttlefish::transport::SharedFdChannel;
using ndk::ScopedAStatus;

class SecureElement : public BnSecureElement {
  public:
    SecureElement(std::shared_ptr<SharedFdChannel> channel);
    ScopedAStatus init(const std::shared_ptr<ISecureElementCallback>& client_callback) override;
    ScopedAStatus getAtr(std::vector<uint8_t>* aidl_return) override;
    ScopedAStatus reset() override;
    ScopedAStatus isCardPresent(bool* aidl_return) override;
    ScopedAStatus openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2,
                                   std::vector<uint8_t>* aidl_return) override;
    ScopedAStatus openLogicalChannel(
        const std::vector<uint8_t>& aid, int8_t p2,
        ::aidl::android::hardware::secure_element::LogicalChannelResponse* aidl_return) override;
    ScopedAStatus closeChannel(int8_t channel_number) override;
    ScopedAStatus transmit(const std::vector<uint8_t>& data,
                           std::vector<uint8_t>* aidl_return) override;

  private:
    Result<void> forwardCommand(const std::vector<uint8_t>& req, std::vector<uint8_t>& res);
    Result<ManagedMessage> toMessage(const std::vector<uint8_t>& message);
    Result<std::vector<uint8_t>> fromMessage(ManagedMessage& message);
    std::shared_ptr<ISecureElementCallback> callback_;
    std::shared_ptr<SharedFdChannel> channel_;
};

}  // namespace aidl::android::hardware::secure_element
