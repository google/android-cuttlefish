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
#include <sys/socket.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace cuttlefish {

/// Defines operations supported by allocd
enum class RequestType : uint16_t {
  Invalid = 0,       // Invalid Request
  ID,                // Allocate and return a new Session ID
  CreateInterface,   // Request to create new network interface
  DestroyInterface,  // Request to destroy a managed network interface
  StopSession,       // Request all resources within a session be released
  Shutdown,          // request allocd to shutdown and clean up all resources
};

/// Defines interface types supported by allocd
enum class IfaceType : uint16_t {
  Invalid = 0,  // an invalid interface
  mtap,         // mobile tap
  wtap,         // bridged wireless tap
  wifiap,       // non bridged wireless tap
  etap,         // ethernet tap
  wbr,          // wireless bridge
  ebr           // ethernet bridge
};

enum class RequestStatus : uint16_t {
  Invalid = 0,  // Invalid status
  Pending,      // Request which has not been attempted
  Success,      // Request was satisfied
  Failure       // Request failed
};

/// Defines the format for allocd Request messages
struct RequestHeader {
  uint16_t version;  /// used to differentiate between allocd feature sets
  uint16_t len;      /// length in bytes of the message payload
};

/// Provides a wrapper around libjson's Reader to additionally log errors
class JsonRequestReader {
 public:
  JsonRequestReader() = default;

  ~JsonRequestReader() = default;

  std::optional<Json::Value> parse(std::string msg) {
    Json::Value ret;
    std::unique_ptr<Json::CharReader> reader(reader_builder.newCharReader());
    std::string errorMessage;
    if (!reader->parse(&*msg.begin(), &*msg.end(), &ret, &errorMessage)) {
      LOG(WARNING) << "Received invalid JSON object in input channel: "
                   << errorMessage;
      LOG(INFO) << "Invalid JSON: " << msg;
      return std::nullopt;
    }
    return ret;
  }

 private:
  Json::CharReaderBuilder reader_builder;
};

}  // namespace cuttlefish
