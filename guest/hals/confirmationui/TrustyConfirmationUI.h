/*
 * Copyright 2020, The Android Open Source Project
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

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include <aidl/android/hardware/confirmationui/BnConfirmationUI.h>
#include <aidl/android/hardware/confirmationui/IConfirmationResultCallback.h>
#include <aidl/android/hardware/confirmationui/UIOption.h>
#include <aidl/android/hardware/security/keymint/HardwareAuthToken.h>
#include <teeui/generic_messages.h>

#include "common/libs/concurrency/thread_safe_queue.h"
#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_fd.h"
#include "guest_session.h"

namespace aidl::android::hardware::confirmationui {

using ::aidl::android::hardware::security::keymint::HardwareAuthToken;
using std::shared_ptr;
using std::string;
using std::vector;

class TrustyConfirmationUI : public BnConfirmationUI {
  public:
    using ConfUiMessage = cuttlefish::confui::ConfUiMessage;
    using ConfUiAckMessage = cuttlefish::confui::ConfUiAckMessage;
    using ListenerState = GuestSession::ListenerState;

    TrustyConfirmationUI();
    virtual ~TrustyConfirmationUI();
    // Methods from ::android::hardware::confirmationui::V1_0::IConfirmationUI
    // follow.
    ::ndk::ScopedAStatus
    promptUserConfirmation(const shared_ptr<IConfirmationResultCallback>& resultCB,
                           const vector<uint8_t>& promptText, const vector<uint8_t>& extraData,
                           const string& locale, const vector<UIOption>& uiOptions) override;
    ::ndk::ScopedAStatus
    deliverSecureInputEvent(const HardwareAuthToken& secureInputToken) override;

    ::ndk::ScopedAStatus abort() override;

  private:
    /*
     * Note for implementation
     *
     * The TEE UI session cannot be pre-emptied normally. The session will have an
     * exclusive control for the input and the screen. Only when something goes
     * wrong, it can be aborted by abort().
     *
     * Another thing is that promptUserConfirmation() may return without waiting
     * for the resultCB is completed. When it returns early, it still returns
     * ResponseCode::OK. In that case, the promptUserConfirmation() could actually
     * fail -- e.g. the input device is broken down afterwards, the user never
     * gave an input until timeout, etc. Then, the resultCB would be called with
     * an appropriate error code. However, even in that case, most of the time
     * promptUserConfirmation() returns OK. Only when the initial set up for
     * confirmation UI fails, promptUserConfirmation() may return non-OK.
     *
     * So, the implementation is roughly:
     *   1. If there's another session going on, return with ResponseCode::Ignored
     *      and the return is immediate
     *   2. If there's a zombie, collect the zombie and go to 3
     *   3. If there's nothing, start a new session in a new thread, and return
     *      the promptUserConfirmation() call as early as possible
     *
     * Another issue is to maintain/define the ownership of vsock. For now,
     * a message fetcher (from the host) will see if the vsock is ok, and
     * reconnect if not. But, eventually, the new session should establish a
     * new connection/client vsock, and the new session should own the fetcher
     * thread.
     */
    std::thread callback_thread_;
    ListenerState listener_state_;

    std::mutex listener_state_lock_;
    std::condition_variable listener_state_condv_;
    int prompt_result_;

    // client virtio-console fd to the host
    cuttlefish::SharedFD host_fd_;

    // ack, response, command from the host, and the abort command from the guest
    std::atomic<std::uint32_t> current_session_id_;
    std::mutex current_session_lock_;
    std::unique_ptr<GuestSession> current_session_;
    std::thread host_cmd_fetcher_thread_;
    bool is_supported_vm_;

    cuttlefish::SharedFD ConnectToHost();
    void HostMessageFetcherLoop();
    void RunSession(shared_ptr<IConfirmationResultCallback> resultCB, string promptText,
                    vector<uint8_t> extraData, string locale, vector<UIOption> uiOptions);
    static const char* GetVirtioConsoleDevicePath();
};

}  // namespace aidl::android::hardware::confirmationui
