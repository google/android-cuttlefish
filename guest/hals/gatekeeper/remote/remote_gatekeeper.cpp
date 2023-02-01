/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "RemoteGateKeeper"

#include "remote_gatekeeper.h"

#include <endian.h>
#include <limits>

#include <android-base/logging.h>
#include <gatekeeper/password_handle.h>
#include <hardware/hw_auth_token.h>

namespace aidl::android::hardware::gatekeeper {

using ::gatekeeper::ERROR_INVALID;
using ::gatekeeper::ERROR_MEMORY_ALLOCATION_FAILED;
using ::gatekeeper::ERROR_NONE;
using ::gatekeeper::ERROR_RETRY;
using ::gatekeeper::ERROR_UNKNOWN;
using ::gatekeeper::SizedBuffer;

RemoteGateKeeperDevice::RemoteGateKeeperDevice(
    cuttlefish::SharedFdGatekeeperChannel* channel)
    : gatekeeper_channel_(channel), error_(0) {}

RemoteGateKeeperDevice::~RemoteGateKeeperDevice() {}

SizedBuffer vec2sized_buffer(const std::vector<uint8_t>& vec) {
    if (vec.size() == 0 || vec.size() > std::numeric_limits<uint32_t>::max()) return {};
    auto unused = new uint8_t[vec.size()];
    std::copy(vec.begin(), vec.end(), unused);
    return {unused, static_cast<uint32_t>(vec.size())};
}

void sizedBuffer2AidlHWToken(SizedBuffer& buffer,
                             android::hardware::security::keymint::HardwareAuthToken* aidlToken) {
    const hw_auth_token_t* authToken =
        reinterpret_cast<const hw_auth_token_t*>(buffer.Data<uint8_t>());
    aidlToken->challenge = authToken->challenge;
    aidlToken->userId = authToken->user_id;
    aidlToken->authenticatorId = authToken->authenticator_id;
    // these are in network order: translate to host
    aidlToken->authenticatorType =
        static_cast<android::hardware::security::keymint::HardwareAuthenticatorType>(
            be32toh(authToken->authenticator_type));
    aidlToken->timestamp.milliSeconds = be64toh(authToken->timestamp);
    aidlToken->mac.insert(aidlToken->mac.begin(), std::begin(authToken->hmac),
                          std::end(authToken->hmac));
}

::ndk::ScopedAStatus
RemoteGateKeeperDevice::enroll(int32_t uid, const std::vector<uint8_t>& currentPasswordHandle,
                               const std::vector<uint8_t>& currentPassword,
                               const std::vector<uint8_t>& desiredPassword,
                               GatekeeperEnrollResponse* rsp) {
    if (error_ != 0) {
        LOG(ERROR) << "Gatekeeper in invalid state";
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
    }

    if (desiredPassword.size() == 0) {
        LOG(ERROR) << "Desired password size is 0";
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
    }

    if (currentPasswordHandle.size() > 0) {
        if (currentPasswordHandle.size() != sizeof(::gatekeeper::password_handle_t)) {
            LOG(ERROR) << "Password handle has wrong length";
            return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
        }
    }

    EnrollRequest request(uid, vec2sized_buffer(currentPasswordHandle),
                          vec2sized_buffer(desiredPassword), vec2sized_buffer(currentPassword));
    EnrollResponse response;
    auto error = Send(request, &response);
    if (error != ERROR_NONE) {
        LOG(ERROR) << "Enroll request gave error: " << error;
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
    } else if (response.error == ERROR_RETRY) {
        LOG(ERROR) << "Enroll response has a retry error";
        *rsp = {ERROR_RETRY_TIMEOUT, static_cast<int32_t>(response.retry_timeout), 0, {}};
        return ndk::ScopedAStatus::ok();
    } else if (response.error != ERROR_NONE) {
        LOG(ERROR) << "Enroll response has an error: " << response.error;
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
    } else {
        const ::gatekeeper::password_handle_t* password_handle =
            response.enrolled_password_handle.Data<::gatekeeper::password_handle_t>();
        *rsp = {STATUS_OK,
                0,
                static_cast<int64_t>(password_handle->user_id),
                {response.enrolled_password_handle.Data<uint8_t>(),
                 (response.enrolled_password_handle.Data<uint8_t>() +
                  response.enrolled_password_handle.size())}};
    }
    return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RemoteGateKeeperDevice::verify(
    int32_t uid, int64_t challenge, const std::vector<uint8_t>& enrolledPasswordHandle,
    const std::vector<uint8_t>& providedPassword, GatekeeperVerifyResponse* rsp) {
    if (error_ != 0) {
        LOG(ERROR) << "Gatekeeper in invalid state";
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
    }

    if (enrolledPasswordHandle.size() == 0) {
        LOG(ERROR) << "Enrolled password size is 0";
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
    }

    if (enrolledPasswordHandle.size() > 0) {
        if (enrolledPasswordHandle.size() != sizeof(::gatekeeper::password_handle_t)) {
            LOG(ERROR) << "Password handle has wrong length";
            return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
        }
    }

    VerifyRequest request(uid, challenge, vec2sized_buffer(enrolledPasswordHandle),
                          vec2sized_buffer(providedPassword));
    VerifyResponse response;

    auto error = Send(request, &response);
    if (error != ERROR_NONE) {
        LOG(ERROR) << "Verify request gave error: " << error;
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
    } else if (response.error == ERROR_RETRY) {
        LOG(ERROR) << "Verify request response gave retry error";
        *rsp = {ERROR_RETRY_TIMEOUT, static_cast<int32_t>(response.retry_timeout), {}};
        return ndk::ScopedAStatus::ok();
    } else if (response.error != ERROR_NONE) {
        LOG(ERROR) << "Verify request response gave error: " << response.error;
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_GENERAL_FAILURE));
    } else {
        // On Success, return GatekeeperVerifyResponse with Success Status, timeout{0} and
        // valid HardwareAuthToken.
        *rsp = {response.request_reenroll ? STATUS_REENROLL : STATUS_OK, 0, {}};
        // Convert the hw_auth_token_t to HardwareAuthToken in the response.
        sizedBuffer2AidlHWToken(response.auth_token, &rsp->hardwareAuthToken);
    }
    return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RemoteGateKeeperDevice::deleteUser(int32_t /*uid*/) {
    LOG(ERROR) << "deleteUser is unimplemented";
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_NOT_IMPLEMENTED));
}

::ndk::ScopedAStatus RemoteGateKeeperDevice::deleteAllUsers() {
    LOG(ERROR) << "deleteAllUsers is unimplemented";
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(ERROR_NOT_IMPLEMENTED));
}

gatekeeper_error_t RemoteGateKeeperDevice::Send(uint32_t command, const GateKeeperMessage& request,
                                                GateKeeperMessage* response) {
    if (!gatekeeper_channel_->SendRequest(command, request)) {
        LOG(ERROR) << "Failed to send request";
        return ERROR_UNKNOWN;
    }
    auto remote_response = gatekeeper_channel_->ReceiveMessage();
    if (!remote_response) {
        LOG(ERROR) << "Failed to receive response";
        return ERROR_UNKNOWN;
    }
    const uint8_t* buffer = remote_response->payload;
    const uint8_t* buffer_end = remote_response->payload + remote_response->payload_size;
    auto rc = response->Deserialize(buffer, buffer_end);
    if (rc != ERROR_NONE) {
        LOG(ERROR) << "Failed to deserialize keymaster response: " << command;
        return ERROR_UNKNOWN;
    }
    return rc;
}

};  // namespace aidl::android::hardware::gatekeeper
