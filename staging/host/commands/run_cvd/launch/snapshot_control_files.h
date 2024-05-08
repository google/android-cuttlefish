//
// Copyright (C) 2023 The Android Open Source Project
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

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {

struct SnapshotControlFiles {
  SharedFD confui_server_fd;
  SharedFD secure_env_snapshot_control_fd;
  SharedFD run_cvd_to_secure_env_fd;

  static Result<SnapshotControlFiles> Create(
      const CuttlefishConfig::InstanceSpecific&);
};

using AutoSnapshotControlFiles = AutoSetup<SnapshotControlFiles::Create>;

}  // namespace cuttlefish
