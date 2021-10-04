//
// Copyright (C) 2020 The Android Open Source Project
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

#include <json/json.h>
#include <tss2/tss2_tpm2_types.h>

namespace cuttlefish {

/**
 * Data storage tailored to Gatekeeper's storage needs: storing binary blobs
 * that can be destroyed without a trace or corrupted with an obvious trace, but
 * not silently tampered with or read by an unauthorized user.
 *
 * Data can be stored through Write and retrieved through Read. To delete data,
 * issue a Write that overwrites the data to destroy it.
 */
class GatekeeperStorage {
public:
  virtual ~GatekeeperStorage() = default;

  virtual bool Allocate(const Json::Value& key, uint16_t size) = 0;
  virtual bool HasKey(const Json::Value& key) const = 0;

  virtual std::unique_ptr<TPM2B_MAX_NV_BUFFER> Read(const Json::Value& key)
      const = 0;
  virtual bool Write(const Json::Value& key, const TPM2B_MAX_NV_BUFFER& data)
      = 0;
};

}  // namespace cuttlefish
