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

#pragma once

#include <memory>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/instance_group_record.h"

namespace cuttlefish {

constexpr char kDefaultOPeratorControlSocketPath[] =
    "/run/cuttlefish/operator_control";

/**
 * OperatorControlConn represents a connection to the Operator's control socket.
 */
class OperatorControlConn {
 public:
  static Result<std::unique_ptr<OperatorControlConn>> Create(
      const std::string& socket_path = kDefaultOPeratorControlSocketPath);

  /**
   * Pre-registers an instance group with the operator
   */
  Result<void> Preregister(const selector::LocalInstanceGroup& group);

 private:
  OperatorControlConn(SharedFD conn) : conn_(conn) {}

  OperatorControlConn(OperatorControlConn&&) = default;
  OperatorControlConn(const OperatorControlConn&) = delete;
  OperatorControlConn& operator=(const OperatorControlConn&) = delete;

  SharedFD conn_;
};

}  // namespace cuttlefish
