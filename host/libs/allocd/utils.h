/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <android-base/logging.h>
#include <json/json.h>

#include <optional>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/allocd/request.h"
#include "host/libs/config/logging.h"

namespace cuttlefish {

static constexpr int send_flags = 0;
static constexpr int recv_flags = 0;

// returns true if successfully sent the whole message
bool SendJsonMsg(cuttlefish::SharedFD client_socket, const Json::Value& resp);

std::optional<Json::Value> RecvJsonMsg(cuttlefish::SharedFD client_socket);

}  // namespace cuttlefish
