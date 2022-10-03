/*
 *
 * Copyright 2021, The Android Open Source Project
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

#include <aidl/android/hardware/security/keymint/HardwareAuthToken.h>
#include <android-base/logging.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "common/libs/concurrency/thread_safe_queue.h"
#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_fd.h"

namespace aidl::android::hardware::confirmationui {
using ::aidl::android::hardware::security::keymint::HardwareAuthToken;
class GuestSession {
  public:
    using ConfUiMessage = cuttlefish::confui::ConfUiMessage;
    using ConfUiAckMessage = cuttlefish::confui::ConfUiAckMessage;
    using Queue = cuttlefish::ThreadSafeQueue<std::unique_ptr<ConfUiMessage>>;
    using QueueImpl = Queue::QueueImpl;

    enum class ListenerState : uint32_t {
        None = 0,
        Starting = 1,
        SetupDone = 2,
        Interactive = 3,
        Terminating = 4,
    };

    GuestSession(const std::uint32_t session_id, ListenerState& listener_state,
                 std::mutex& listener_state_lock, std::condition_variable& listener_state_condv,
                 cuttlefish::SharedFD host_fd, const teeui::MsgString& promptText,
                 const teeui::MsgVector<uint8_t>& extraData, const teeui::MsgString& locale,
                 const teeui::MsgVector<teeui::UIOption>& uiOptions)
        : prompt_text_{promptText.begin(), promptText.end()}, extra_data_{extraData.begin(),
                                                                          extraData.end()},
          locale_{locale.begin(), locale.end()}, ui_options_{uiOptions.begin(), uiOptions.end()},
          listener_state_(listener_state), listener_state_lock_(listener_state_lock),
          listener_state_condv_(listener_state_condv), host_fd_{host_fd},
          session_name_(MakeName(session_id)),
          incoming_msg_queue_(
              20, [this](GuestSession::QueueImpl* impl) { return QueueFullHandler(impl); }) {}

    ~GuestSession() {
        // the thread for PromptUserConfirmation is still alive
        // the host_fd_ may be alive
        auto state = listener_state_;
        if (state == ListenerState::SetupDone || state == ListenerState::Interactive) {
            Abort();
        }
        // TODO(kwstephenkim): close fd once Session takes the ownership of fd
        // join host_cmd_fetcher_thread_ once Session takes the ownership of fd
    }

    using ResultTriple = std::tuple<int, teeui::MsgVector<uint8_t>, teeui::MsgVector<uint8_t>>;
    ResultTriple PromptUserConfirmation();

    int DeliverSecureInputEvent(const HardwareAuthToken& secureInputToken);

    void Abort();
    std::string GetSessionId() const { return session_name_; }

    void Push(std::unique_ptr<ConfUiMessage>&& msg) { incoming_msg_queue_.Push(std::move(msg)); }

  private:
    template <typename F, typename... Args>
    bool SerializedSend(F&& f, cuttlefish::SharedFD fd, Args&&... args) {
        if (!fd->IsOpen()) {
            return false;
        }
        std::unique_lock<std::mutex> lock(send_serializer_mtx_);
        return f(fd, std::forward<Args>(args)...);
    }

    void QueueFullHandler(QueueImpl* queue_impl) {
        if (!queue_impl) {
            LOG(ERROR) << "Registered queue handler is "
                       << "seeing nullptr for queue implementation.";
            return;
        }
        const auto n = (queue_impl->size()) / 2;
        // pop front half
        queue_impl->erase(queue_impl->begin(), queue_impl->begin() + n);
    }

    std::string MakeName(const std::uint32_t i) const {
        return "ConfirmationUiSession" + std::to_string(i);
    }
    std::string prompt_text_;
    std::vector<std::uint8_t> extra_data_;
    std::string locale_;
    std::vector<teeui::UIOption> ui_options_;

    /*
     * lister_state_lock_ coordinates multiple threads that may
     * call the three Confirmation UI HAL APIs concurrently
     */
    ListenerState& listener_state_;
    std::mutex& listener_state_lock_;
    std::condition_variable& listener_state_condv_;
    cuttlefish::SharedFD host_fd_;

    const std::string session_name_;
    Queue incoming_msg_queue_;

    /*
     * multiple threads could try to write on the vsock at the
     * same time. E.g. promptUserConfirmation() thread sends
     * a command while abort() is being called. The abort() thread
     * will try to write an abort command concurrently.
     */
    std::mutex send_serializer_mtx_;
};
}  // namespace aidl::android::hardware::confirmationui
