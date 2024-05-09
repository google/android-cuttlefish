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
#include "json/json.h"

#include <optional>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/config/logging.h"
#include "request.h"

namespace cuttlefish {

constexpr char kDefaultLocation[] =
    "/var/run/cuttlefish/cuttlefish_allocd.sock";

// Default flags for send and receive.
static constexpr int kSendFlags = 0;
static constexpr int kRecvFlags = 0;

/// Sends a Json value over client_socket
///
/// returns true if successfully sent the whole JSON object
/// returns false otherwise
bool SendJsonMsg(cuttlefish::SharedFD client_socket, const Json::Value& resp);

/// Receives a single Json value over client_socket
///
/// The returned option will contain the JSON object when successful,
/// or an std::nullopt if an error is reported
std::optional<Json::Value> RecvJsonMsg(cuttlefish::SharedFD client_socket);

// Helper functions mapping between Enum types and std::string

RequestType StrToReqTy(const std::string& req);

std::string ReqTyToStr(RequestType req_ty);

IfaceType StrToIfaceTy(const std::string& iface);

std::string IfaceTyToStr(IfaceType iface);

RequestStatus StrToStatus(const std::string& st);

std::string StatusToStr(RequestStatus st);

}  // namespace cuttlefish
