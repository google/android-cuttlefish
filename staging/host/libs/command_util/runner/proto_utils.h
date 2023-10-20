/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <string>

#include "common/libs/utils/result.h"
#include "host/libs/command_util/runner/defs.h"

namespace cuttlefish {

struct RequestInfo {
  std::string serialized_data;
  ExtendedActionType extended_action_type;
};

Result<std::string> SerializeSuspendRequest();
Result<std::string> SerializeResumeRequest();
Result<std::string> SerializeSnapshotTakeRequest(
    const std::string& snapshot_path);
Result<std::string> SerializeStartScreenRecordingRequest();
Result<std::string> SerializeStopScreenRecordingRequest();

}  // namespace cuttlefish
