/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <stdio.h>
#include <binder_rpc_unstable.hpp>

#include <aidl/com/android/minidroid/testservice/BnTestService.h>
#include <string>

namespace {

void start_test_service() {
  class TestService
      : public aidl::com::android::minidroid::testservice::BnTestService {
    ndk::ScopedAStatus sayHello() override {
      printf("Hello World!\n");
      return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus printText(const std::string& text) override {
      printf("%s\n", text.c_str());
      return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus addInteger(int32_t a, int32_t b, int32_t* out) override {
      *out = a + b;
      return ndk::ScopedAStatus::ok();
    }
  };
  auto testService = ndk::SharedRefBase::make<TestService>();

  ARpcServer* server = ARpcServer_newVsock(testService->asBinder().get(), 2,
                                           testService->SERVICE_PORT);

  printf("Calling join on server!\n");
  ARpcServer_join(server);
}
}  // namespace

int main() {
  printf("Hello Minidroid server!\n");

  start_test_service();

  return 0;
}
