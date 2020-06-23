#pragma once

/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <memory>

#include <host/libs/config/cuttlefish_config.h>

// Handles initialization of regions that require it strictly before the virtual
// machine is started.
// To add initializers for more regions declare here, implement in its own
// source file and call from PreLaunchInitializers::Initialize().
void InitializeScreenRegion(const cuttlefish::CuttlefishConfig& config);

class PreLaunchInitializers {
 public:
  static void Initialize(const cuttlefish::CuttlefishConfig& config) {
    InitializeScreenRegion(config);
  }
};
