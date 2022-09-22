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

#include <future>
#include <optional>
#include <string>

#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

class GceInstanceDisk {
 public:
  GceInstanceDisk() = default;
  explicit GceInstanceDisk(const Json::Value&);

  static GceInstanceDisk EphemeralBootDisk();

  std::optional<std::string> Name() const;
  GceInstanceDisk& Name(const std::string&) &;
  GceInstanceDisk Name(const std::string&) &&;

  std::optional<std::string> SourceImage() const;
  GceInstanceDisk& SourceImage(const std::string&) &;
  GceInstanceDisk SourceImage(const std::string&) &&;

  GceInstanceDisk& SizeGb(uint64_t gb) &;
  GceInstanceDisk SizeGb(uint64_t gb) &&;

  const Json::Value& AsJson() const;

 private:
  Json::Value data_;
};

class GceNetworkInterface {
 public:
  GceNetworkInterface() = default;
  explicit GceNetworkInterface(const Json::Value&);

  static GceNetworkInterface Default();

  std::optional<std::string> Network() const;
  GceNetworkInterface& Network(const std::string&) &;
  GceNetworkInterface Network(const std::string&) &&;

  std::optional<std::string> Subnetwork() const;
  GceNetworkInterface& Subnetwork(const std::string&) &;
  GceNetworkInterface Subnetwork(const std::string&) &&;

  std::optional<std::string> ExternalIp() const;
  std::optional<std::string> InternalIp() const;

  const Json::Value& AsJson() const;

 private:
  Json::Value data_;
};

class GceInstanceInfo {
 public:
  GceInstanceInfo() = default;
  explicit GceInstanceInfo(const Json::Value&);

  std::optional<std::string> Zone() const;
  GceInstanceInfo& Zone(const std::string&) &;
  GceInstanceInfo Zone(const std::string&) &&;

  std::optional<std::string> Name() const;
  GceInstanceInfo& Name(const std::string&) &;
  GceInstanceInfo Name(const std::string&) &&;

  std::optional<std::string> MachineType() const;
  GceInstanceInfo& MachineType(const std::string&) &;
  GceInstanceInfo MachineType(const std::string&) &&;

  GceInstanceInfo& AddDisk(const GceInstanceDisk&) &;
  GceInstanceInfo AddDisk(const GceInstanceDisk&) &&;

  GceInstanceInfo& AddNetworkInterface(const GceNetworkInterface&) &;
  GceInstanceInfo AddNetworkInterface(const GceNetworkInterface&) &&;
  std::vector<GceNetworkInterface> NetworkInterfaces() const;

  GceInstanceInfo& AddMetadata(const std::string&, const std::string&) &;
  GceInstanceInfo AddMetadata(const std::string&, const std::string&) &&;

  GceInstanceInfo& AddScope(const std::string&) &;
  GceInstanceInfo AddScope(const std::string&) &&;

  const Json::Value& AsJson() const;

 private:
  Json::Value data_;
};

class GceApi {
 public:
  class Operation {
   public:
    ~Operation();
    void StopWaiting();
    /// `true` means it waited to completion, `false` means it was cancelled
    std::future<Result<bool>>& Future();

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    Operation(std::unique_ptr<Impl>);
    friend class GceApi;
  };

  GceApi(HttpClient&, CredentialSource& credentials,
         const std::string& project);

  std::future<Result<GceInstanceInfo>> Get(const GceInstanceInfo&);
  std::future<Result<GceInstanceInfo>> Get(const std::string& zone,
                                           const std::string& name);

  Operation Insert(const Json::Value&);
  Operation Insert(const GceInstanceInfo&);

  Operation Reset(const std::string& zone, const std::string& name);
  Operation Reset(const GceInstanceInfo&);

  Operation Delete(const std::string& zone, const std::string& name);
  Operation Delete(const GceInstanceInfo&);

 private:
  Result<std::vector<std::string>> Headers();

  HttpClient& http_client_;
  CredentialSource& credentials_;
  std::string project_;
};

}  // namespace cuttlefish
