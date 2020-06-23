//
// Copyright (C) 2020 The Android Open Source Project
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

#include "logging.h"

#include <android-base/logging.h>

#include "common/libs/utils/tee_logging.h"
#include "host/libs/config/cuttlefish_config.h"

using android::base::SetLogger;

namespace cuttlefish {

void DefaultSubprocessLogging(char* argv[]) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  auto config = cuttlefish::CuttlefishConfig::Get();

  CHECK(config) << "Could not open cuttlefish config";

  auto instance = config->ForDefaultInstance();

  if (config->run_as_daemon()) {
    SetLogger(LogToFiles({instance.launcher_log_path()}));
  } else {
    SetLogger(LogToStderrAndFiles({instance.launcher_log_path()}));
  }
}

} // namespace cuttlefish
