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

#include "SecureElement.h"

#include <string>

#include <android-base/logging.h>

namespace aidl::android::hardware::secure_element {
constexpr const int kUnusedCommandField = 0;
constexpr int32_t kSuccess = 0x9000;

namespace {
using cuttlefish::ErrorFromType;
using cuttlefish::OutcomeDereference;
using cuttlefish::StackTraceEntry;
using cuttlefish::TypeIsSuccess;

Result<void> ResponseOK(const std::vector<uint8_t>& response) {
    CF_EXPECT(response.size() >= 2, "Response Size less than 2");
    size_t size = response.size();
    CF_EXPECT(((response[size - 2] << 8) | response[size - 1]) == kSuccess,
              "Status Code: " << (response[size - 2] << 8 | response[size - 1]));
    return {};
}

}  // namespace

SecureElement::SecureElement(std::shared_ptr<SharedFdChannel> channel) : channel_(channel) {}

Result<ManagedMessage> SecureElement::toMessage(const std::vector<uint8_t>& data) {
    auto msg = CF_EXPECT(cuttlefish::transport::CreateMessage(kUnusedCommandField, data.size()));
    std::copy(data.begin(), data.end(), msg->payload);
    return msg;
}

Result<std::vector<uint8_t>> SecureElement::fromMessage(ManagedMessage& message) {
    std::vector<uint8_t> res;
    const uint8_t* buffer = message->payload;
    const uint8_t* buffer_end = message->payload + message->payload_size;
    if (message->payload_size > 0) {
        res.insert(res.begin(), buffer, buffer_end);
    }
    return res;
}

Result<void> SecureElement::forwardCommand(const std::vector<uint8_t>& req,
                                           std::vector<uint8_t>& res) {
    auto msg = CF_EXPECT(toMessage(req), "Failed to create message from the request");
    CF_EXPECT(channel_->SendRequest(*msg), "Failed to send request");
    CF_EXPECT(channel_->WaitForMessage(), "Failed to wait for command response");
    auto response = CF_EXPECT(channel_->ReceiveMessage(), "Failed to receive response");
    auto result = CF_EXPECT(fromMessage(response), "Failed to read from Message");
    res = std::move(result);
    return {};
}

ScopedAStatus SecureElement::init(const std::shared_ptr<ISecureElementCallback>& client_callback) {
    if (client_callback == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    callback_ = client_callback;
    callback_->onStateChange(true, "init");
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::getAtr(std::vector<uint8_t>* aidl_return) {
    if (callback_ == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    std::vector<uint8_t> const atr{};
    *aidl_return = atr;
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::isCardPresent(bool* aidl_return) {
    if (callback_ == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    *aidl_return = true;
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::reset() {
    if (callback_ == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    callback_->onStateChange(false, "reset");
    callback_->onStateChange(true, "reset");
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::transmit(const std::vector<uint8_t>& data,
                                      std::vector<uint8_t>* aidl_return) {
    if (callback_ == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    std::vector<uint8_t> output;
    if (!forwardCommand(data, output).ok()) {
        LOG(ERROR) << "Failed to transmit.";
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }
    *aidl_return = output;
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::openLogicalChannel(
    const std::vector<uint8_t>& aid, int8_t p2,
    ::aidl::android::hardware::secure_element::LogicalChannelResponse* aidl_return) {
    if (callback_ == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    std::vector<uint8_t> resApduBuff;
    std::vector<uint8_t> manageChannelCommand = {0x00, 0x70, 0x00, 0x00, 0x01};

    // send manage command (optional) but will need in FiRa multi-channel
    // implementation
    if (!forwardCommand(manageChannelCommand, resApduBuff).ok()) {
        LOG(ERROR) << "Failed to send ManageChannel request";
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }
    auto result = ResponseOK(resApduBuff);
    if (!result.ok()) {
        LOG(ERROR) << "Failed in ManageChannelCommand - " << result.error().Message();
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }

    std::vector<uint8_t> selectCmd;
    size_t channelNumber = 1;
    if ((resApduBuff[0] > 0x03) && (resApduBuff[0] < 0x14)) {
        /* update CLA byte according to GP spec Table 11-12*/
        selectCmd.push_back(0x40 + (resApduBuff[0] - 4)); /* Class of instruction */
    } else if ((resApduBuff[0] > 0x00) && (resApduBuff[0] < 0x04)) {
        /* update CLA byte according to GP spec Table 11-11*/
        selectCmd.push_back((uint8_t)resApduBuff[0]); /* Class of instruction */
    } else {
        LOG(ERROR) << "Invalid Channel " << resApduBuff[0];
        resApduBuff[0] = 0xff;
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }
    channelNumber = selectCmd[0];

    // send select command
    selectCmd.push_back((uint8_t)0xA4);        /* Instruction code */
    selectCmd.push_back((uint8_t)0x04);        /* Instruction parameter 1 */
    selectCmd.push_back(p2);                   /* Instruction parameter 2 */
    selectCmd.push_back((uint8_t)aid.size());  // should be fine as AID is always less than 128
    selectCmd.insert(selectCmd.end(), aid.begin(), aid.end());
    selectCmd.push_back((uint8_t)256);

    resApduBuff.clear();
    if (!forwardCommand(selectCmd, resApduBuff).ok()) {
        LOG(ERROR) << "Failed to send openLogicalChannel request.";
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }

    result = ResponseOK(resApduBuff);
    if (!result.ok()) {
        LOG(ERROR) << "Failed to open logical channel - " << result.error().Message();
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }

    aidl_return->channelNumber = static_cast<int8_t>(channelNumber);
    aidl_return->selectResponse = std::move(resApduBuff);
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2,
                                              std::vector<uint8_t>* aidl_return) {
    // send select command
    std::vector<uint8_t> selectCmd;
    std::vector<uint8_t> resApduBuff;

    selectCmd.push_back((uint8_t)0x00);        /* CLA - Basic Channel 0 */
    selectCmd.push_back((uint8_t)0xA4);        /* Instruction code */
    selectCmd.push_back((uint8_t)0x04);        /* Instruction parameter 1 */
    selectCmd.push_back(p2);                   /* Instruction parameter 2 */
    selectCmd.push_back((uint8_t)aid.size());  // should be fine as AID is always less than 128
    selectCmd.insert(selectCmd.end(), aid.begin(), aid.end());
    selectCmd.push_back((uint8_t)256);

    if (!forwardCommand(selectCmd, resApduBuff).ok()) {
        LOG(ERROR) << "Failed to send openBasicChannel request.";
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }
    auto result = ResponseOK(resApduBuff);
    if (!result.ok()) {
        LOG(ERROR) << "Failed to open basic channel - " << result.error().Message();
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }

    *aidl_return = resApduBuff;
    return ScopedAStatus::ok();
}

ScopedAStatus SecureElement::closeChannel(int8_t channelNumber) {
    if (callback_ == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    std::vector<uint8_t> manageChannelCommand = {0x00, 0x70, 0x80, 0x00, 0x00};
    std::vector<uint8_t> resApduBuff;

    // change class of instruction & p2 parameter
    manageChannelCommand[0] = channelNumber;
    // For Supplementary Channel update CLA byte according to GP
    if ((channelNumber > 0x03) && (channelNumber < 0x14)) {
        /* update CLA byte according to GP spec Table 11-12*/
        manageChannelCommand[0] = 0x40 + (channelNumber - 4);
    }
    manageChannelCommand[3] = channelNumber; /* Instruction parameter 2 */

    if (!forwardCommand(manageChannelCommand, resApduBuff).ok()) {
        LOG(ERROR) << "Failed to send closeChannel request.";
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }

    auto result = ResponseOK(resApduBuff);
    if (!result.ok()) {
        LOG(ERROR) << "closeChannel failed - " << result.error().Message();
        return ScopedAStatus::fromServiceSpecificError(IOERROR);
    }

    return ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::secure_element
