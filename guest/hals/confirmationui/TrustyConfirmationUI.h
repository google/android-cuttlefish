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

#ifndef ANDROID_HARDWARE_CONFIRMATIONUI_V1_0_TRUSTY_CONFIRMATIONUI_H
#define ANDROID_HARDWARE_CONFIRMATIONUI_V1_0_TRUSTY_CONFIRMATIONUI_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include <android/hardware/confirmationui/1.0/IConfirmationUI.h>
#include <android/hardware/keymaster/4.0/types.h>
#include <hidl/Status.h>
#include <teeui/generic_messages.h>

#include "common/libs/concurrency/thread_safe_queue.h"
#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_fd.h"
#include "guest_session.h"

namespace android {
namespace hardware {
namespace confirmationui {
namespace V1_0 {
namespace implementation {

using ::android::sp;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

class TrustyConfirmationUI : public IConfirmationUI {
  public:
    using ConfUiMessage = cuttlefish::confui::ConfUiMessage;
    using ConfUiAckMessage = cuttlefish::confui::ConfUiAckMessage;
    using ListenerState = GuestSession::ListenerState;

    TrustyConfirmationUI();
    virtual ~TrustyConfirmationUI();
    // Methods from ::android::hardware::confirmationui::V1_0::IConfirmationUI
    // follow.
    Return<ResponseCode> promptUserConfirmation(const sp<IConfirmationResultCallback>& resultCB,
                                                const hidl_string& promptText,
                                                const hidl_vec<uint8_t>& extraData,
                                                const hidl_string& locale,
                                                const hidl_vec<UIOption>& uiOptions) override;
    Return<ResponseCode> deliverSecureInputEvent(
        const ::android::hardware::keymaster::V4_0::HardwareAuthToken& secureInputToken) override;

    Return<void> abort() override;

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
    ResponseCode prompt_result_;

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
    void RunSession(sp<IConfirmationResultCallback> resultCB, hidl_string promptText,
                    hidl_vec<uint8_t> extraData, hidl_string locale, hidl_vec<UIOption> uiOptions);
    static const char* GetVirtioConsoleDevicePath();
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace confirmationui
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_CONFIRMATIONUI_V1_0_TRUSTY_CONFIRMATIONUI_H
