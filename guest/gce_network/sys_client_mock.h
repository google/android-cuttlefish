/*
 * Copyright (C) 2016 The Android Open Source Project
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
#ifndef GUEST_GCE_NETWORK_SYS_CLIENT_MOCK_H_
#define GUEST_GCE_NETWORK_SYS_CLIENT_MOCK_H_

#include <cstdint>

#include <gmock/gmock.h>

#include "guest/gce_network/sys_client.h"

namespace avd {
namespace test {

class MockSysClient : public SysClient {
 public:
  MOCK_METHOD3(Clone, ProcessHandle*(
      const std::string&, const std::function<int32_t()>&, int32_t));
  MOCK_METHOD2(SetNs, int32_t(int32_t, int32_t));
  MOCK_METHOD1(Unshare, int32_t(int32_t));
  MOCK_METHOD1(POpen, ProcessPipe*(const std::string&));
  MOCK_METHOD1(System, int32_t(const std::string&));
  MOCK_METHOD2(Umount, int32_t(const std::string&, int32_t));
  MOCK_METHOD4(Mount, int32_t(const std::string&, const std::string&,
                              const std::string&, int32_t));
  MOCK_METHOD3(Socket, int32_t(int, int, int));
  MOCK_METHOD3(IoCtl, int32_t(int, int, void*));
  MOCK_METHOD3(SendMsg, int32_t(int, struct msghdr*, int32_t));
  MOCK_METHOD3(RecvMsg, int32_t(int, struct msghdr*, int32_t));
  MOCK_METHOD1(Close, int32_t(int));
};

}  // namespace test
}  // namespace avd

#endif  // GUEST_GCE_NETWORK_SYS_CLIENT_MOCK_H_
