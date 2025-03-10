//
// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <teeui/common_message_types.h>  // /system/teeui/libteeui/.../include

#include "common/libs/confui/packet_types.h"
#include "common/libs/confui/protocol_types.h"
#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace confui {

std::string ToString(const ConfUiMessage& msg);

constexpr auto SESSION_ANY = "";
/*
 * received confirmation UI message on the guest could be abort or
 * ack/response. Thus, the guest APIs should call RecvConfUiMsg(fd),
 * see which is it, and then use Into*(conf_ui_message) to
 * parse & use it.
 *
 */
std::unique_ptr<ConfUiMessage> RecvConfUiMsg(SharedFD fd);
std::unique_ptr<ConfUiMessage> RecvConfUiMsg(const std::string& session_id,
                                             SharedFD fd);

bool SendAbortCmd(SharedFD fd, const std::string& session_id);

bool SendAck(SharedFD fd, const std::string& session_id, const bool is_success,
             const std::string& status_message);
bool SendResponse(SharedFD fd, const std::string& session_id,
                  const UserResponse::type& plain_selection,
                  const std::vector<std::uint8_t>& signed_response,
                  // signing is a function of message, key
                  const std::vector<std::uint8_t>& message);

// for HAL
bool SendStartCmd(SharedFD fd, const std::string& session_id,
                  const std::string& prompt_text,
                  const std::vector<std::uint8_t>& extra_data,
                  const std::string& locale,
                  const std::vector<teeui::UIOption>& ui_opts);

bool SendStopCmd(SharedFD fd, const std::string& session_id);

// for HAL::deliverSecureInputEvent
bool SendUserSelection(SharedFD fd, const std::string& session_id,
                       const UserResponse::type& confirm_cancel);

}  // end of namespace confui
}  // end of namespace cuttlefish
