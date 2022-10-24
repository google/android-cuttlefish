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

#pragma once

#include <string>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

struct GCPInstance {
  const char* machine_type;
  const char* min_cpu_platform;
};

struct CreateHostInstanceRequest {
  GCPInstance* gcp;
};

struct BuildInfo {
  const std::string& build_id;
  const std::string& target;
};

struct CreateCVDRequest {
  const BuildInfo& build_info;
};

struct OperationResult {
  Json::Value response;
};

struct Operation {
  bool done;
  OperationResult result;
};

class CloudOrchestratorApi {
 public:
  CloudOrchestratorApi(const std::string& service_url, const std::string& zone,
                       HttpClient& http_client);
  ~CloudOrchestratorApi();

  Result<std::string> CreateHost(const CreateHostInstanceRequest& request);

  Result<Operation> WaitCloudOperation(const std::string& name);

  Result<std::vector<std::string>> ListHosts();

  Result<void> DeleteHost(const std::string& name);

  Result<std::string> CreateCVD(const std::string& host,
                                const CreateCVDRequest& request);

  Result<Operation> WaitHostOperation(const std::string& host,
                                      const std::string& name);

  Result<std::vector<std::string>> ListCVDWebRTCStreams(
      const std::string& host);

 private:
  const std::string& service_url_;
  const std::string& zone_;
  HttpClient& http_client_;
};

}  // namespace cuttlefish
