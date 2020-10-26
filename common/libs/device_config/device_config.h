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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "device/google/cuttlefish/common/libs/device_config/device_config.pb.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/cuttlefish_config.h"
#endif

namespace cuttlefish {

class DeviceConfigHelper {
 public:
  static std::unique_ptr<DeviceConfigHelper> Get();

  const DeviceConfig& GetDeviceConfig() const { return device_config_; }

  bool SendDeviceConfig(SharedFD fd);

 private:
  explicit DeviceConfigHelper(const DeviceConfig& device_config);

  DeviceConfig device_config_;
};

}  // namespace cuttlefish
