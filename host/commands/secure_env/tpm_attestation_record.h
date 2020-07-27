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

#include <keymaster/attestation_record.h>

class TpmAttestationRecordContext : public keymaster::AttestationRecordContext {
public:
  TpmAttestationRecordContext() = default;
  ~TpmAttestationRecordContext() = default;

  keymaster_security_level_t GetSecurityLevel() const override;
  keymaster_error_t VerifyAndCopyDeviceIds(
      const keymaster::AuthorizationSet&,
      keymaster::AuthorizationSet*) const override;
  keymaster_error_t GenerateUniqueId(
      uint64_t, const keymaster_blob_t&, bool, keymaster::Buffer*) const override;
  keymaster_error_t GetVerifiedBootParams(
      keymaster_blob_t*,
      keymaster_blob_t*,
      keymaster_verified_boot_t*,
      bool*) const override;
};
