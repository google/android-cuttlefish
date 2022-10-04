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

#include <iostream>

#include "host/commands/remote/remote.h"

#include "common/libs/utils/json.h"

namespace cuttlefish {
namespace {

const char* kFieldItems = "items";
const char* kFieldName = "name";

static std::string JsonToString(const Json::Value& input) {
  Json::StreamWriterBuilder wbuilder;
  wbuilder["indentation"] = "";
  return Json::writeString(wbuilder, input);
}

static std::string CreateHostBody(const CreateHostInstanceRequest& request) {
  Json::Value gcp;
  gcp["disk_size_gb"] = request.gcp->disk_size_gb;
  gcp["machine_type"] = request.gcp->machine_type;
  gcp["min_cpu_platform"] = request.gcp->min_cpu_platform;
  Json::Value request_json;
  request_json["create_host_instance_request"]["gcp"] = gcp;
  return JsonToString(request_json);
}

static std::string ToJson(const CreateCVDRequest& request) {
  Json::Value build_info;
  build_info["build_id"] = request.build_info.build_id;
  build_info["target"] = request.build_info.target;
  Json::Value root;
  root["build_info"] = build_info;
  return JsonToString(root);
}

}  // namespace

CloudOrchestratorApi::CloudOrchestratorApi(const std::string& service_url,
                                           const std::string& zone,
                                           HttpClient& http_client)
    : service_url_(service_url), zone_(zone), http_client_(http_client) {}

CloudOrchestratorApi::~CloudOrchestratorApi() {}

Result<std::string> CloudOrchestratorApi::CreateHost(
    const CreateHostInstanceRequest& request) {
  std::string url = service_url_ + "/v1/zones/" + zone_ + "/hosts";
  std::string data = CreateHostBody(request);
  auto resp =
      CF_EXPECT(http_client_.PostToString(url, data), "Http client failed");
  CF_EXPECT(resp.HttpSuccess(), "Http request failed with status code: "
                                    << resp.http_code << ", server response:\n"
                                    << resp.data);
  auto resp_json =
      CF_EXPECT(ParseJson(resp.data), "Failed parsing response body");
  CF_EXPECT(
      resp_json.isMember(kFieldName),
      "Invalid create host response,  missing field: '" << kFieldName << "'");
  return resp_json[kFieldName].asString();
}

Result<std::vector<std::string>> CloudOrchestratorApi::ListHosts() {
  std::string url = service_url_ + "/v1/zones/" + zone_ + "/hosts";
  auto resp = CF_EXPECT(http_client_.GetToString(url), "Http client failed");
  CF_EXPECT(resp.HttpSuccess(), "Http request failed with status code: "
                                    << resp.http_code << ", server response:\n"
                                    << resp.data);
  auto root = CF_EXPECT(ParseJson(resp.data), "Failed parsing response body");
  CF_EXPECT(
      root.isMember(kFieldItems),
      "Invalid list hosts response,  missing field: '" << kFieldItems << "'");
  std::vector<std::string> result;
  for (const Json::Value& item : root[kFieldItems]) {
    result.push_back(item["name"].asString());
  }
  return result;
}

Result<std::string> CloudOrchestratorApi::CreateCVD(
    const std::string& host, const CreateCVDRequest& request) {
  std::string url =
      service_url_ + "/v1/zones/" + zone_ + "/hosts/" + host + "/cvds";
  std::string data = ToJson(request);
  auto resp =
      CF_EXPECT(http_client_.PostToString(url, data), "Http client failed");
  CF_EXPECT(resp.HttpSuccess(), "Http request failed with status code: "
                                    << resp.http_code << ", server response:\n"
                                    << resp.data);
  auto resp_json =
      CF_EXPECT(ParseJson(resp.data), "Failed parsing response body");
  CF_EXPECT(
      resp_json.isMember(kFieldName),
      "Invalid create cvd response,  missing field: '" << kFieldName << "'");
  return resp_json[kFieldName].asString();
}

Result<std::vector<std::string>> CloudOrchestratorApi::ListCVDWebRTCStreams(
    const std::string& host) {
  std::string url =
      service_url_ + "/v1/zones/" + zone_ + "/hosts/" + host + "/devices";
  auto resp = CF_EXPECT(http_client_.GetToString(url), "Http client failed");
  CF_EXPECT(resp.HttpSuccess(), "Http request failed with status code: "
                                    << resp.http_code << ", server response:\n"
                                    << resp.data);
  auto root = CF_EXPECT(ParseJson(resp.data), "Failed parsing response body");
  std::vector<std::string> result;
  for (int index = 0; index < root.size(); index++) {
    result.push_back(root[index].asString());
  }
  return result;
}

}  // namespace cuttlefish
