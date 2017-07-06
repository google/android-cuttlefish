#pragma once

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
                     const std::map<std::string, VSoCSharedMemory::Region>&());
};
}  // namespace test
}  // namespace ivserver