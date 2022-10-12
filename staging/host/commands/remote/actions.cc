//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/remote/actions.h"

#include <string>

namespace cuttlefish {
namespace internal {

const char* kFieldName = "name";

// Creates a host to run cvds on it.
class CreateHostAction : public Action<std::string> {
 public:
  CreateHostAction(CloudOrchestratorApi& api,
                   const CreateHostInstanceRequest& request)
      : api_(api), request_(request) {}

  ~CreateHostAction() {}

  Result<std::string> Execute() override {
    auto operation_name =
        CF_EXPECT(api_.CreateHost(request_), "Create host failed");
    auto operation = CF_EXPECT(api_.WaitCloudOperation(operation_name),
                               "Waiting for operation failed");
    if (!operation.done) {
      return CF_ERR("Create host operation is not done yet");
    }
    OperationResult& result = operation.result;
    CF_EXPECT(
        result.response.isMember(kFieldName),
        "Invalid operation response, missing field: '" << kFieldName << "'");
    return result.response[kFieldName].asString();
  }

 private:
  CloudOrchestratorApi& api_;
  const CreateHostInstanceRequest& request_;
};

// Creates a cvd.
class CreateCVDAction : public Action<std::string> {
 public:
  CreateCVDAction(CloudOrchestratorApi& api, const CreateCVDRequest& request,
                  std::string host)
      : api_(api), request_(request), host_(host) {}

  ~CreateCVDAction() {}

  Result<std::string> Execute() override {
    auto operation_name =
        CF_EXPECT(api_.CreateCVD(host_, request_), "Create cvd failed");
    auto operation = CF_EXPECT(api_.WaitHostOperation(host_, operation_name),
                               "Waiting for operation failed");
    if (!operation.done) {
      return CF_ERR("Create cvd operation is not done yet");
    }
    OperationResult& result = operation.result;
    CF_EXPECT(
        result.response.isMember(kFieldName),
        "Invalid operation response, missing field: '" << kFieldName << "'");
    return result.response[kFieldName].asString();
  }

 private:
  CloudOrchestratorApi& api_;
  const CreateCVDRequest& request_;
  std::string host_;
};

}  // namespace internal

std::unique_ptr<Action<std::string>> CreateHostAction(
    CloudOrchestratorApi& api, const CreateHostInstanceRequest& request) {
  return std::unique_ptr<Action<std::string>>(
      new internal::CreateHostAction(api, request));
}

std::unique_ptr<Action<std::string>> CreateCVDAction(
    CloudOrchestratorApi& api, const CreateCVDRequest& request,
    std::string host) {
  return std::unique_ptr<Action<std::string>>(
      new internal::CreateCVDAction(api, request, host));
}

}  // namespace cuttlefish
