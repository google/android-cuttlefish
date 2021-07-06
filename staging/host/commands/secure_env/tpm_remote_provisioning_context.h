/*
 * Copyright 2021 The Android Open Source Project
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

#pragma once

#include <keymaster/remote_provisioning_context.h>

#include "host/commands/secure_env/tpm_resource_manager.h"
#include "keymaster/cppcose/cppcose.h"

/**
 * TPM-backed implementation of the provisioning context.
 */
class TpmRemoteProvisioningContext
    : public keymaster::RemoteProvisioningContext {
 public:
  TpmRemoteProvisioningContext(TpmResourceManager& resource_manager);
  ~TpmRemoteProvisioningContext() override = default;
  std::vector<uint8_t> DeriveBytesFromHbk(const std::string& context,
                                          size_t numBytes) const override;
  std::unique_ptr<cppbor::Map> CreateDeviceInfo() const override;
  std::pair<std::vector<uint8_t>, cppbor::Array> GenerateBcc(
      bool testMode) const override;
  std::optional<cppcose::HmacSha256> GenerateHmacSha256(
      const cppcose::bytevec& input) const override;

 private:
  TpmResourceManager& resource_manager_;
};
