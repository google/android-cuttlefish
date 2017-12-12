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

#include <gmock/gmock.h>

#include "host/ivserver/vsocsharedmem.h"

namespace ivserver {
namespace test {
class VSoCSharedMemoryMock : public VSoCSharedMemory {
 public:
  MOCK_CONST_METHOD3(GetEventFdPairForRegion,
                     bool(const std::string&, avd::SharedFD*, avd::SharedFD*));
  MOCK_CONST_METHOD0(SharedMemFD, const avd::SharedFD&());
  MOCK_CONST_METHOD0(Regions,
                     const std::vector<VSoCSharedMemory::Region>&());
};
}  // namespace test
}  // namespace ivserver
