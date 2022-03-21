/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "host/commands/cvd/server.h"

#include "cvd_server.pb.h"

#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_client.h"

namespace cuttlefish {

namespace {

class TryAcloudCommand : public CvdServerHandler {
 public:
  INJECT(TryAcloudCommand()) = default;
  ~TryAcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    return ParseInvocation(request.Message()).command == "try-acloud";
  }
  Result<cvd::Response> Handle(const RequestWithStdio&) override {
    return CF_ERR("TODO(schuffelen)");
  }
  Result<void> Interrupt() override { return CF_ERR("Can't be interrupted."); }
};

class AcloudCommand : public CvdServerHandler {
 public:
  INJECT(AcloudCommand()) = default;
  ~AcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    return ParseInvocation(request.Message()).command == "acloud";
  }
  Result<cvd::Response> Handle(const RequestWithStdio&) override {
    return CF_ERR("TODO(schuffelen)");
  }
  Result<void> Interrupt() override { return CF_ERR("Can't be interrupted."); }
};

}  // namespace

fruit::Component<> AcloudCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, AcloudCommand>()
      .addMultibinding<CvdServerHandler, TryAcloudCommand>();
}

}  // namespace cuttlefish
