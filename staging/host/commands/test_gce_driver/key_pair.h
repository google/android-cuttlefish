//
// Copyright (C) 2021 The Android Open Source Project
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

#include <memory>
#include <string>

#include <android-base/result.h>

namespace cuttlefish {

struct KeyPair {
 public:
  static android::base::Result<std::unique_ptr<KeyPair>> CreateRsa(
      size_t bytes);
  virtual ~KeyPair() = default;

  virtual android::base::Result<std::string> PemPrivateKey() const = 0;
  virtual android::base::Result<std::string> PemPublicKey() const = 0;
  virtual android::base::Result<std::string> OpenSshPublicKey() const = 0;
};

};  // namespace cuttlefish
