//
// Copyright (C) 2019 The Android Open Source Project
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

#include <chrono>
#include <memory>

#include "curl_wrapper.h"

namespace cuttlefish {

class CredentialSource {
public:
  virtual ~CredentialSource() = default;
  virtual std::string Credential() = 0;
};

class GceMetadataCredentialSource : public CredentialSource {
  CurlWrapper curl;
  std::string latest_credential;
  std::chrono::steady_clock::time_point expiration;

  void RefreshCredential();
public:
  GceMetadataCredentialSource();
  GceMetadataCredentialSource(GceMetadataCredentialSource&&) = default;

  virtual std::string Credential();

  static std::unique_ptr<CredentialSource> make();
};

class FixedCredentialSource : public CredentialSource {
  std::string credential;
public:
  FixedCredentialSource(const std::string& credential);

  virtual std::string Credential();

  static std::unique_ptr<CredentialSource> make(const std::string& credential);
};

}
