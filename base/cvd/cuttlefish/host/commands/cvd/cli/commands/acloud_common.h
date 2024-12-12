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

#include "host/commands/cvd/cli/command_request.h"

namespace cuttlefish {

struct AcloudTranslatorOptOut {};

bool IsSubOperationSupported(const CommandRequest& request);

// Acloud delete is not translated because it needs to handle remote cases.
// Python acloud implements delete by calling stop_cvd
// This function replaces stop_cvd with a script that calls `cvd rm`, which in
// turn calls cvd_internal_stop if necessary.
Result<void> PrepareForAcloudDeleteCommand(const std::string& host_artifacts);

}  // namespace cuttlefish
