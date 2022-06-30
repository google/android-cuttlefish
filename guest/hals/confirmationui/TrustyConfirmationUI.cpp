/*
 *
 * Copyright 2019, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "TrustyConfirmationUI.h"

#include <cutils/properties.h>

namespace android {
namespace hardware {
namespace confirmationui {
namespace V1_0 {
namespace implementation {

using ::teeui::MsgString;
using ::teeui::MsgVector;
using ::android::hardware::keymaster::V4_0::HardwareAuthToken;
using TeeuiRc = ::teeui::ResponseCode;

namespace {
teeui::UIOption convertUIOption(UIOption uio) {
    static_assert(uint32_t(UIOption::AccessibilityInverted) ==
                          uint32_t(teeui::UIOption::AccessibilityInverted) &&
                      uint32_t(UIOption::AccessibilityMagnified) ==
                          uint32_t(teeui::UIOption::AccessibilityMagnified),
                  "teeui::UIOPtion and ::android::hardware::confirmationui::V1_0::UIOption "
                  "are out of sync");
    return teeui::UIOption(uio);
}

inline MsgString hidl2MsgString(const hidl_string& s) {
    return {s.c_str(), s.c_str() + s.size()};
}
template <typename T> inline MsgVector<T> hidl2MsgVector(const hidl_vec<T>& v) {
    return {v};
}

inline MsgVector<teeui::UIOption> hidl2MsgVector(const hidl_vec<UIOption>& v) {
    MsgVector<teeui::UIOption> result(v.size());
    for (unsigned int i = 0; i < v.size(); ++i) {
        result[i] = convertUIOption(v[i]);
    }
    return result;
}
}  // namespace

const char* TrustyConfirmationUI::GetVirtioConsoleDevicePath() {
    static char device_path[] = "/dev/hvc8";
    return device_path;
}

TrustyConfirmationUI::TrustyConfirmationUI()
    : listener_state_(ListenerState::None),
      prompt_result_(ResponseCode::Ignored), current_session_id_{10} {
    host_fd_ = cuttlefish::SharedFD::Open(GetVirtioConsoleDevicePath(), O_RDWR);
    CHECK(host_fd_->IsOpen()) << "ConfUI: " << GetVirtioConsoleDevicePath() << " is not open.";
    CHECK(host_fd_->SetTerminalRaw() >= 0)
        << "ConfUI: " << GetVirtioConsoleDevicePath() << " fail in SetTerminalRaw()";

    constexpr static const auto enable_confirmationui_property = "ro.boot.enable_confirmationui";
    const auto arg = property_get_int32(enable_confirmationui_property, -1);
    is_supported_vm_ = (arg == 1);

    if (host_fd_->IsOpen()) {
        auto fetching_cmd = [this]() { HostMessageFetcherLoop(); };
        host_cmd_fetcher_thread_ = std::thread(fetching_cmd);
    }
}

TrustyConfirmationUI::~TrustyConfirmationUI() {
    if (host_fd_->IsOpen()) {
        host_fd_->Close();
    }
    if (host_cmd_fetcher_thread_.joinable()) {
        host_cmd_fetcher_thread_.join();
    }

    if (listener_state_ != ListenerState::None) {
        callback_thread_.join();
    }
}

void TrustyConfirmationUI::HostMessageFetcherLoop() {
    while (true) {
        if (!host_fd_->IsOpen()) {
            // this happens when TrustyConfirmationUI is destroyed
            ConfUiLog(ERROR) << "host_fd_ is not open";
            return;
        }
        ConfUiLog(INFO) << "Trying to fetch command";
        auto msg = cuttlefish::confui::RecvConfUiMsg(host_fd_);
        ConfUiLog(INFO) << "RecvConfUiMsg() returned";
        if (!msg) {
            // virtio-console is broken for now
            ConfUiLog(ERROR) << "received message was null";
            return;
        }
        {
            std::unique_lock<std::mutex> lk(current_session_lock_);
            if (!current_session_ || msg->GetSessionId() != current_session_->GetSessionId()) {
                if (!current_session_) {
                    ConfUiLog(ERROR) << "msg is received but session is null";
                    continue;
                }
                ConfUiLog(ERROR) << "session id mismatch, so ignored"
                                 << "Received for " << msg->GetSessionId()
                                 << " but currently running " << current_session_->GetSessionId();
                continue;
            }
            current_session_->Push(std::move(msg));
        }
        listener_state_condv_.notify_all();
    }
}

void TrustyConfirmationUI::RunSession(sp<IConfirmationResultCallback> resultCB,
                                      hidl_string promptText, hidl_vec<uint8_t> extraData,
                                      hidl_string locale, hidl_vec<UIOption> uiOptions) {
    cuttlefish::SharedFD fd = host_fd_;
    // ownership of the fd is passed to GuestSession
    {
        std::unique_lock<std::mutex> lk(current_session_lock_);
        current_session_ = std::make_unique<GuestSession>(
            current_session_id_, listener_state_, listener_state_lock_, listener_state_condv_, fd,
            hidl2MsgString(promptText), hidl2MsgVector(extraData), hidl2MsgString(locale),
            hidl2MsgVector(uiOptions));
    }

    auto [rc, msg, token] = current_session_->PromptUserConfirmation();

    std::unique_lock<std::mutex> lock(listener_state_lock_);  // for listener_state_
    bool do_callback = (listener_state_ == ListenerState::Interactive ||
                        listener_state_ == ListenerState::SetupDone) &&
                       resultCB;
    prompt_result_ = rc;
    listener_state_ = ListenerState::Terminating;
    lock.unlock();
    if (do_callback) {
        auto error = resultCB->result(prompt_result_, msg, token);
        if (!error.isOk()) {
            ConfUiLog(ERROR) << "Result callback failed " << error.description();
        }
        ConfUiLog(INFO) << "Result callback returned.";
    } else {
        listener_state_condv_.notify_all();
    }
}

// Methods from ::android::hardware::confirmationui::V1_0::IConfirmationUI
// follow.
Return<ResponseCode> TrustyConfirmationUI::promptUserConfirmation(
    const sp<IConfirmationResultCallback>& resultCB, const hidl_string& promptText,
    const hidl_vec<uint8_t>& extraData, const hidl_string& locale,
    const hidl_vec<UIOption>& uiOptions) {
    std::unique_lock<std::mutex> stateLock(listener_state_lock_, std::defer_lock);
    ConfUiLog(INFO) << "promptUserConfirmation is called";
    if (!is_supported_vm_) {
        resultCB->result(ResponseCode::Unimplemented, {}, {});
    }
    if (!stateLock.try_lock()) {
        return ResponseCode::OperationPending;
    }
    switch (listener_state_) {
    case ListenerState::None:
        break;
    case ListenerState::Starting:
    case ListenerState::SetupDone:
    case ListenerState::Interactive:
        return ResponseCode::OperationPending;
    case ListenerState::Terminating:
        callback_thread_.join();
        listener_state_ = ListenerState::None;
        break;
    default:
        return ResponseCode::Unexpected;
    }
    assert(listener_state_ == ListenerState::None);
    listener_state_ = ListenerState::Starting;

    current_session_id_++;
    auto worker = [this](const sp<IConfirmationResultCallback>& resultCB,
                         const hidl_string& promptText, const hidl_vec<uint8_t>& extraData,
                         const hidl_string& locale, const hidl_vec<UIOption>& uiOptions) {
        RunSession(resultCB, promptText, extraData, locale, uiOptions);
    };
    callback_thread_ = std::thread(worker, resultCB, promptText, extraData, locale, uiOptions);

    listener_state_condv_.wait(stateLock, [this] {
        return listener_state_ == ListenerState::SetupDone ||
               listener_state_ == ListenerState::Interactive ||
               listener_state_ == ListenerState::Terminating;
    });
    if (listener_state_ == ListenerState::Terminating) {
        callback_thread_.join();
        listener_state_ = ListenerState::None;
        if (prompt_result_ == ResponseCode::Canceled) {
            // VTS expects this
            return ResponseCode::OK;
        }
        return prompt_result_;
    }
    return ResponseCode::OK;
}

Return<ResponseCode>
TrustyConfirmationUI::deliverSecureInputEvent(const HardwareAuthToken& auth_token) {
    ConfUiLog(INFO) << "deliverSecureInputEvent is called";
    ResponseCode rc = ResponseCode::Ignored;
    if (!is_supported_vm_) {
        return ResponseCode::Unimplemented;
    }
    {
        std::unique_lock<std::mutex> lock(current_session_lock_);
        if (!current_session_) {
            return rc;
        }
        return current_session_->DeliverSecureInputEvent(auth_token);
    }
}

Return<void> TrustyConfirmationUI::abort() {
    if (!is_supported_vm_) return {};
    std::unique_lock<std::mutex> lock(current_session_lock_);
    if (!current_session_) {
        return Void();
    }
    return current_session_->Abort();
}

android::sp<IConfirmationUI> createTrustyConfirmationUI() {
    return new TrustyConfirmationUI();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace confirmationui
}  // namespace hardware
}  // namespace android
