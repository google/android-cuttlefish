//
// Copyright (C) 2019 The Android Open Source Project
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
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

class KernelLogPipeProvider : public virtual SetupFeature {
 public:
  virtual ~KernelLogPipeProvider() = default;
  virtual SharedFD KernelLogPipe() = 0;
};

/** Parent class tag for classes that inject KernelLogPipe. */
class KernelLogPipeConsumer {};

}  // namespace cuttlefish
