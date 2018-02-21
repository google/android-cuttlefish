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

#include <inttypes.h>
#include <json/json.h>
#include <map>
#include <memory>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "uapi/vsoc_shm.h"

namespace ivserver {

class VSoCSharedMemory {
 public:
  // Region describes a VSoCSharedMem region.
  struct Region {
    vsoc_device_region values{};
    cvd::SharedFD host_fd;
    cvd::SharedFD guest_fd;
  };

  VSoCSharedMemory() = default;
  virtual ~VSoCSharedMemory() = default;

  static std::unique_ptr<VSoCSharedMemory> New(const std::string &name,
                                               const Json::Value &json_root);

  virtual bool GetEventFdPairForRegion(const std::string &region_name,
                                       cvd::SharedFD *guest_to_host,
                                       cvd::SharedFD *host_to_guest) const = 0;

  virtual const cvd::SharedFD &SharedMemFD() const = 0;
  virtual const std::vector<Region> &Regions() const = 0;

 private:
  VSoCSharedMemory(const VSoCSharedMemory &) = delete;
  VSoCSharedMemory &operator=(const VSoCSharedMemory &other) = delete;
};

}  // namespace ivserver
