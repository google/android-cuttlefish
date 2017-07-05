#pragma once

#include <gmock/gmock.h>

#include "host/ivserver/vsocsharedmem.h"

namespace ivserver {
namespace test {
class VSoCSharedMemoryMock : public VSoCSharedMemory {
 public:
  MOCK_CONST_METHOD3(GetEventFdPairForRegion, bool(const std::string&, avd::SharedFD*, avd::SharedFD*));
  MOCK_CONST_METHOD0(shared_mem_fd, const avd::SharedFD&());
  MOCK_CONST_METHOD1(BroadcastQemuSocket, void(const avd::SharedFD&));
};
}
}