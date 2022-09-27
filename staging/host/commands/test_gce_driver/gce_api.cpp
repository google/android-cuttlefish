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

#include "host/commands/test_gce_driver/gce_api.h"

#include <uuid.h>

#include <sstream>
#include <string>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "host/libs/web/credential_source.h"

namespace cuttlefish {

std::optional<std::string> OptStringMember(const Json::Value& jn,
                                           const std::string& name) {
  if (!jn.isMember(name) || jn[name].type() != Json::ValueType::stringValue) {
    return {};
  }
  return jn[name].asString();
}

const Json::Value* OptObjMember(const Json::Value& jn,
                                const std::string& name) {
  if (!jn.isMember(name) || jn[name].type() != Json::ValueType::objectValue) {
    return nullptr;
  }
  return &(jn[name]);
}

const Json::Value* OptArrayMember(const Json::Value& jn,
                                  const std::string& name) {
  if (!jn.isMember(name) || jn[name].type() != Json::ValueType::arrayValue) {
    return nullptr;
  }
  return &(jn[name]);
}

Json::Value& EnsureObjMember(Json::Value& jn, const std::string& name) {
  if (!jn.isMember(name) || jn[name].type() != Json::ValueType::objectValue) {
    jn[name] = Json::Value(Json::ValueType::objectValue);
  }
  return jn[name];
}

Json::Value& EnsureArrayMember(Json::Value& jn, const std::string& name) {
  if (!jn.isMember(name) || jn[name].type() != Json::ValueType::arrayValue) {
    jn[name] = Json::Value(Json::ValueType::arrayValue);
  }
  return jn[name];
}

GceInstanceDisk::GceInstanceDisk(const Json::Value& json) : data_(json){};

GceInstanceDisk GceInstanceDisk::EphemeralBootDisk() {
  Json::Value initial_json(Json::ValueType::objectValue);
  initial_json["type"] = "PERSISTENT";
  initial_json["boot"] = true;
  initial_json["mode"] = "READ_WRITE";
  initial_json["autoDelete"] = true;
  return GceInstanceDisk(initial_json);
}

constexpr char kGceDiskInitParams[] = "initializeParams";
constexpr char kGceDiskName[] = "diskName";
std::optional<std::string> GceInstanceDisk::Name() const {
  const auto& init_params = OptObjMember(data_, kGceDiskInitParams);
  if (!init_params) {
    return {};
  }
  return OptStringMember(*init_params, kGceDiskName);
}
GceInstanceDisk& GceInstanceDisk::Name(const std::string& source) & {
  EnsureObjMember(data_, kGceDiskInitParams)[kGceDiskName] = source;
  return *this;
}
GceInstanceDisk GceInstanceDisk::Name(const std::string& source) && {
  EnsureObjMember(data_, kGceDiskInitParams)[kGceDiskName] = source;
  return *this;
}

constexpr char kGceDiskSourceImage[] = "sourceImage";
std::optional<std::string> GceInstanceDisk::SourceImage() const {
  const auto& init_params = OptObjMember(data_, kGceDiskInitParams);
  if (!init_params) {
    return {};
  }
  return OptStringMember(*init_params, kGceDiskSourceImage);
}
GceInstanceDisk& GceInstanceDisk::SourceImage(const std::string& source) & {
  EnsureObjMember(data_, kGceDiskInitParams)[kGceDiskSourceImage] = source;
  return *this;
}
GceInstanceDisk GceInstanceDisk::SourceImage(const std::string& source) && {
  EnsureObjMember(data_, kGceDiskInitParams)[kGceDiskSourceImage] = source;
  return *this;
}

constexpr char kGceDiskSizeGb[] = "diskSizeGb";
GceInstanceDisk& GceInstanceDisk::SizeGb(uint64_t size) & {
  EnsureObjMember(data_, kGceDiskInitParams)[kGceDiskSizeGb] = size;
  return *this;
}
GceInstanceDisk GceInstanceDisk::SizeGb(uint64_t size) && {
  EnsureObjMember(data_, kGceDiskInitParams)[kGceDiskSizeGb] = size;
  return *this;
}

const Json::Value& GceInstanceDisk::AsJson() const { return data_; }

GceNetworkInterface::GceNetworkInterface(const Json::Value& data)
    : data_(data) {}

constexpr char kNetwork[] = "network";
constexpr char kGceNetworkAccessConfigs[] = "accessConfigs";
GceNetworkInterface GceNetworkInterface::Default() {
  Json::Value json{Json::ValueType::objectValue};
  json[kNetwork] = "global/networks/default";
  Json::Value accessConfig{Json::ValueType::objectValue};
  accessConfig["type"] = "ONE_TO_ONE_NAT";
  accessConfig["name"] = "External NAT";
  EnsureArrayMember(json, kGceNetworkAccessConfigs).append(accessConfig);
  return GceNetworkInterface(json);
}

std::optional<std::string> GceNetworkInterface::Network() const {
  return OptStringMember(data_, kNetwork);
}
GceNetworkInterface& GceNetworkInterface::Network(
    const std::string& network) & {
  data_[kNetwork] = network;
  return *this;
}
GceNetworkInterface GceNetworkInterface::Network(
    const std::string& network) && {
  data_[kNetwork] = network;
  return *this;
}

constexpr char kSubnetwork[] = "subnetwork";
std::optional<std::string> GceNetworkInterface::Subnetwork() const {
  return OptStringMember(data_, kSubnetwork);
}
GceNetworkInterface& GceNetworkInterface::Subnetwork(
    const std::string& subnetwork) & {
  data_[kSubnetwork] = subnetwork;
  return *this;
}
GceNetworkInterface GceNetworkInterface::Subnetwork(
    const std::string& subnetwork) && {
  data_[kSubnetwork] = subnetwork;
  return *this;
}

constexpr char kGceNetworkExternalIp[] = "natIP";
std::optional<std::string> GceNetworkInterface::ExternalIp() const {
  auto accessConfigs = OptArrayMember(data_, kGceNetworkAccessConfigs);
  if (!accessConfigs || accessConfigs->size() < 1) {
    return {};
  }
  if ((*accessConfigs)[0].type() != Json::ValueType::objectValue) {
    return {};
  }
  return OptStringMember((*accessConfigs)[0], kGceNetworkExternalIp);
}

constexpr char kGceNetworkInternalIp[] = "networkIP";
std::optional<std::string> GceNetworkInterface::InternalIp() const {
  return OptStringMember(data_, kGceNetworkInternalIp);
}

const Json::Value& GceNetworkInterface::AsJson() const { return data_; }

GceInstanceInfo::GceInstanceInfo(const Json::Value& json) : data_(json) {}

constexpr char kGceZone[] = "zone";
std::optional<std::string> GceInstanceInfo::Zone() const {
  return OptStringMember(data_, kGceZone);
}
GceInstanceInfo& GceInstanceInfo::Zone(const std::string& zone) & {
  data_[kGceZone] = zone;
  return *this;
}
GceInstanceInfo GceInstanceInfo::Zone(const std::string& zone) && {
  data_[kGceZone] = zone;
  return *this;
}

constexpr char kGceName[] = "name";
std::optional<std::string> GceInstanceInfo::Name() const {
  return OptStringMember(data_, kGceName);
}
GceInstanceInfo& GceInstanceInfo::Name(const std::string& name) & {
  data_[kGceName] = name;
  return *this;
}
GceInstanceInfo GceInstanceInfo::Name(const std::string& name) && {
  data_[kGceName] = name;
  return *this;
}

constexpr char kGceMachineType[] = "machineType";
std::optional<std::string> GceInstanceInfo::MachineType() const {
  return OptStringMember(data_, kGceMachineType);
}
GceInstanceInfo& GceInstanceInfo::MachineType(const std::string& type) & {
  data_[kGceMachineType] = type;
  return *this;
}
GceInstanceInfo GceInstanceInfo::MachineType(const std::string& type) && {
  data_[kGceMachineType] = type;
  return *this;
}

constexpr char kGceDisks[] = "disks";
GceInstanceInfo& GceInstanceInfo::AddDisk(const GceInstanceDisk& disk) & {
  EnsureArrayMember(data_, kGceDisks).append(disk.AsJson());
  return *this;
}
GceInstanceInfo GceInstanceInfo::AddDisk(const GceInstanceDisk& disk) && {
  EnsureArrayMember(data_, kGceDisks).append(disk.AsJson());
  return *this;
}

constexpr char kGceNetworkInterfaces[] = "networkInterfaces";
GceInstanceInfo& GceInstanceInfo::AddNetworkInterface(
    const GceNetworkInterface& net) & {
  EnsureArrayMember(data_, kGceNetworkInterfaces).append(net.AsJson());
  return *this;
}
GceInstanceInfo GceInstanceInfo::AddNetworkInterface(
    const GceNetworkInterface& net) && {
  EnsureArrayMember(data_, kGceNetworkInterfaces).append(net.AsJson());
  return *this;
}
std::vector<GceNetworkInterface> GceInstanceInfo::NetworkInterfaces() const {
  auto jsonNetworkInterfaces = OptArrayMember(data_, kGceNetworkInterfaces);
  if (!jsonNetworkInterfaces) {
    return {};
  }
  std::vector<GceNetworkInterface> interfaces;
  for (const Json::Value& jsonNetworkInterface : *jsonNetworkInterfaces) {
    interfaces.push_back(GceNetworkInterface(jsonNetworkInterface));
  }
  return interfaces;
}

constexpr char kGceMetadata[] = "metadata";
constexpr char kGceMetadataItems[] = "items";
constexpr char kGceMetadataKey[] = "key";
constexpr char kGceMetadataValue[] = "value";
GceInstanceInfo& GceInstanceInfo::AddMetadata(const std::string& key,
                                              const std::string& value) & {
  Json::Value item{Json::ValueType::objectValue};
  item[kGceMetadataKey] = key;
  item[kGceMetadataValue] = value;
  auto& metadata = EnsureObjMember(data_, kGceMetadata);
  EnsureArrayMember(metadata, kGceMetadataItems).append(item);
  return *this;
}
GceInstanceInfo GceInstanceInfo::AddMetadata(const std::string& key,
                                             const std::string& value) && {
  Json::Value item{Json::ValueType::objectValue};
  item[kGceMetadataKey] = key;
  item[kGceMetadataValue] = value;
  auto& metadata = EnsureObjMember(data_, kGceMetadata);
  EnsureArrayMember(metadata, kGceMetadataItems).append(item);
  return *this;
}

constexpr char kGceServiceAccounts[] = "serviceAccounts";
constexpr char kGceScopes[] = "scopes";
GceInstanceInfo& GceInstanceInfo::AddScope(const std::string& scope) & {
  auto& serviceAccounts = EnsureArrayMember(data_, kGceServiceAccounts);
  if (serviceAccounts.size() == 0) {
    serviceAccounts.append(Json::Value(Json::ValueType::objectValue));
  }
  serviceAccounts[0]["email"] = "default";
  auto& scopes = EnsureArrayMember(serviceAccounts[0], kGceScopes);
  scopes.append(scope);
  return *this;
}
GceInstanceInfo GceInstanceInfo::AddScope(const std::string& scope) && {
  auto& serviceAccounts = EnsureArrayMember(data_, kGceServiceAccounts);
  if (serviceAccounts.size() == 0) {
    serviceAccounts.append(Json::Value(Json::ValueType::objectValue));
  }
  serviceAccounts[0]["email"] = "default";
  auto& scopes = EnsureArrayMember(serviceAccounts[0], kGceScopes);
  scopes.append(scope);
  return *this;
}

const Json::Value& GceInstanceInfo::AsJson() const { return data_; }

GceApi::GceApi(HttpClient& http_client, CredentialSource& credentials,
               const std::string& project)
    : http_client_(http_client), credentials_(credentials), project_(project) {}

Result<std::vector<std::string>> GceApi::Headers() {
  return {{
      "Authorization:Bearer " + CF_EXPECT(credentials_.Credential()),
      "Content-Type: application/json",
  }};
}

class GceApi::Operation::Impl {
 public:
  Impl(GceApi& gce_api, std::function<Result<Json::Value>()> initial_request)
      : gce_api_(gce_api), initial_request_(std::move(initial_request)) {
    operation_future_ = std::async([this]() { return Run(); });
  }

  Result<bool> Run() {
    auto initial_response =
        CF_EXPECT(initial_request_(), "Initial request failed: ");

    auto url = OptStringMember(initial_response, "selfLink");
    if (!url) {
      return CF_ERR("Operation " << initial_response
                                 << " was missing `selfLink` field.");
    }
    url = *url + "/wait";
    running_ = true;

    while (running_) {
      auto response = CF_EXPECT(gce_api_.http_client_.PostToJson(
          *url, std::string{""}, CF_EXPECT(gce_api_.Headers())));
      const auto& json = response.data;
      Json::Value errors;
      if (auto j_error = OptObjMember(json, "error"); j_error) {
        if (auto j_errors = OptArrayMember(*j_error, "errors"); j_errors) {
          errors = j_errors->size() > 0 ? *j_errors : Json::Value();
        }
      }
      Json::Value warnings;
      if (auto j_warnings = OptArrayMember(json, "warnings"); j_warnings) {
        warnings = j_warnings->size() > 0 ? *j_warnings : Json::Value();
      }
      LOG(DEBUG) << "Requested operation status at \"" << *url
                 << "\", received " << json;
      if (!response.HttpSuccess() || errors != Json::Value()) {
        return CF_ERR("Error accessing \"" << *url << "\". Errors: " << errors
                                           << ", Warnings: " << warnings);
      }
      if (!json.isMember("status") ||
          json["status"].type() != Json::ValueType::stringValue) {
        return CF_ERR(json << " \"status\" field invalid");
      }
      if (json["status"] == "DONE") {
        return true;
      }
    }
    return false;
  }

 private:
  GceApi& gce_api_;
  std::function<Result<Json::Value>()> initial_request_;
  bool running_;
  std::future<Result<bool>> operation_future_;
  friend class GceApi::Operation;
};

GceApi::Operation::Operation(std::unique_ptr<GceApi::Operation::Impl> impl)
    : impl_(std::move(impl)) {}

GceApi::Operation::~Operation() = default;

void GceApi::Operation::StopWaiting() { impl_->running_ = false; }

std::future<Result<bool>>& GceApi::Operation::Future() {
  return impl_->operation_future_;
}

static std::string RandomUuid() {
  uuid_t uuid;
  uuid_generate_random(uuid);
  std::string uuid_str = "xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx";
  uuid_unparse(uuid, uuid_str.data());
  return uuid_str;
}

// GCE gives back full URLs for zones, but it only wants the last part in
// requests
static std::string SanitizeZone(const std::string& zone) {
  auto last_slash = zone.rfind("/");
  if (last_slash == std::string::npos) {
    return zone;
  }
  return zone.substr(last_slash + 1);
}

std::future<Result<GceInstanceInfo>> GceApi::Get(
    const GceInstanceInfo& instance) {
  auto name = instance.Name();
  if (!name) {
    auto task = [json = instance.AsJson()]() -> Result<GceInstanceInfo> {
      return CF_ERR("Missing a name for \"" << json << "\"");
    };
    return std::async(std::launch::deferred, task);
  }
  auto zone = instance.Zone();
  if (!zone) {
    auto task = [json = instance.AsJson()]() -> Result<GceInstanceInfo> {
      return CF_ERR("Missing a zone for \"" << json << "\"");
    };
    return std::async(std::launch::deferred, task);
  }
  return Get(*zone, *name);
}

std::future<Result<GceInstanceInfo>> GceApi::Get(const std::string& zone,
                                                 const std::string& name) {
  std::stringstream url;
  url << "https://compute.googleapis.com/compute/v1";
  url << "/projects/" << http_client_.UrlEscape(project_);
  url << "/zones/" << http_client_.UrlEscape(SanitizeZone(zone));
  url << "/instances/" << http_client_.UrlEscape(name);
  auto task = [this, url = url.str()]() -> Result<GceInstanceInfo> {
    auto response =
        CF_EXPECT(http_client_.DownloadToJson(url, CF_EXPECT(Headers())));
    if (!response.HttpSuccess()) {
      return CF_ERR("Failed to get instance info, received "
                    << response.data << " with code " << response.http_code);
    }
    return GceInstanceInfo(response.data);
  };
  return std::async(task);
}

GceApi::Operation GceApi::Insert(const Json::Value& request) {
  if (!request.isMember("zone") ||
      request["zone"].type() != Json::ValueType::stringValue) {
    auto task = [request]() -> Result<Json::Value> {
      return CF_ERR("Missing a zone for \"" << request << "\"");
    };
    return Operation(
        std::unique_ptr<Operation::Impl>(new Operation::Impl(*this, task)));
  }
  auto zone = request["zone"].asString();
  Json::Value requestNoZone = request;
  requestNoZone.removeMember("zone");
  std::stringstream url;
  url << "https://compute.googleapis.com/compute/v1";
  url << "/projects/" << http_client_.UrlEscape(project_);
  url << "/zones/" << http_client_.UrlEscape(SanitizeZone(zone));
  url << "/instances";
  url << "?requestId=" << RandomUuid();  // Avoid duplication on request retry
  auto task = [this, requestNoZone, url = url.str()]() -> Result<Json::Value> {
    auto response = CF_EXPECT(
        http_client_.PostToJson(url, requestNoZone, CF_EXPECT(Headers())));
    if (!response.HttpSuccess()) {
      return CF_ERR("Failed to create instance: "
                    << response.data << ". Sent request " << requestNoZone);
    }
    return response.data;
  };
  return Operation(
      std::unique_ptr<Operation::Impl>(new Operation::Impl(*this, task)));
}

GceApi::Operation GceApi::Insert(const GceInstanceInfo& request) {
  return Insert(request.AsJson());
}

GceApi::Operation GceApi::Reset(const std::string& zone,
                                const std::string& name) {
  std::stringstream url;
  url << "https://compute.googleapis.com/compute/v1";
  url << "/projects/" << http_client_.UrlEscape(project_);
  url << "/zones/" << http_client_.UrlEscape(SanitizeZone(zone));
  url << "/instances/" << http_client_.UrlEscape(name);
  url << "/reset";
  url << "?requestId=" << RandomUuid();  // Avoid duplication on request retry
  auto task = [this, url = url.str()]() -> Result<Json::Value> {
    auto response = CF_EXPECT(
        http_client_.PostToJson(url, Json::Value(), CF_EXPECT(Headers())));
    if (!response.HttpSuccess()) {
      return CF_ERR("Failed to create instance: " << response.data);
    }
    return response.data;
  };
  return Operation(
      std::unique_ptr<Operation::Impl>(new Operation::Impl(*this, task)));
}

GceApi::Operation GceApi::Reset(const GceInstanceInfo& instance) {
  auto name = instance.Name();
  if (!name) {
    auto task = [json = instance.AsJson()]() -> Result<Json::Value> {
      return CF_ERR("Missing a name for \"" << json << "\"");
    };
    return Operation(
        std::unique_ptr<Operation::Impl>(new Operation::Impl(*this, task)));
  }
  auto zone = instance.Zone();
  if (!zone) {
    auto task = [json = instance.AsJson()]() -> Result<Json::Value> {
      return CF_ERR("Missing a zone for \"" << json << "\"");
    };
    return Operation(
        std::unique_ptr<Operation::Impl>(new Operation::Impl(*this, task)));
  }
  return Reset(*zone, *name);
}

GceApi::Operation GceApi::Delete(const std::string& zone,
                                 const std::string& name) {
  std::stringstream url;
  url << "https://compute.googleapis.com/compute/v1";
  url << "/projects/" << http_client_.UrlEscape(project_);
  url << "/zones/" << http_client_.UrlEscape(SanitizeZone(zone));
  url << "/instances/" << http_client_.UrlEscape(name);
  url << "?requestId=" << RandomUuid();  // Avoid duplication on request retry
  auto task = [this, url = url.str()]() -> Result<Json::Value> {
    auto response =
        CF_EXPECT(http_client_.DeleteToJson(url, CF_EXPECT(Headers())));
    if (!response.HttpSuccess()) {
      return CF_ERR("Failed to delete instance: " << response.data);
    }
    return response.data;
  };
  return Operation(
      std::unique_ptr<Operation::Impl>(new Operation::Impl(*this, task)));
}

GceApi::Operation GceApi::Delete(const GceInstanceInfo& instance) {
  auto name = instance.Name();
  if (!name) {
    auto task = [json = instance.AsJson()]() -> Result<Json::Value> {
      return CF_ERR("Missing a name for \"" << json << "\"");
    };
    return Operation(
        std::unique_ptr<Operation::Impl>(new Operation::Impl(*this, task)));
  }
  auto zone = instance.Zone();
  if (!zone) {
    auto task = [json = instance.AsJson()]() -> Result<Json::Value> {
      return CF_ERR("Missing a zone for \"" << json << "\"");
    };
    return Operation(
        std::unique_ptr<Operation::Impl>(new Operation::Impl(*this, task)));
  }
  return Delete(*zone, *name);
}

}  // namespace cuttlefish
