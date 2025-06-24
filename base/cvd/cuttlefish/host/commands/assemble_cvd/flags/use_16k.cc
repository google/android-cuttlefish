/*
 * Copyright (C) 2019 The Android Open Source Project
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
#include "cuttlefish/host/commands/assemble_cvd/flags/use_16k.h"

#include <gflags/gflags.h>

DEFINE_bool(use_16k, false, "Launch using 16k kernel");

namespace cuttlefish {

Use16kFlag Use16kFlag::FromGlobalGflags() { return Use16kFlag(FLAGS_use_16k); }

bool Use16kFlag::Use16k() const { return use_16k_; }

Use16kFlag::Use16kFlag(bool use_16k) : use_16k_(use_16k) {}

}  // namespace cuttlefish
