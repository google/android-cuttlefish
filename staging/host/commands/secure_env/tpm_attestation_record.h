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

#include <memory>

#include <keymaster/attestation_context.h>

class TpmAttestationRecordContext : public keymaster::AttestationContext {
public:
 TpmAttestationRecordContext()
     : keymaster::AttestationContext(::keymaster::KmVersion::KEYMINT_1) {}
 ~TpmAttestationRecordContext() = default;

 keymaster_security_level_t GetSecurityLevel() const override;
 keymaster_error_t VerifyAndCopyDeviceIds(
     const keymaster::AuthorizationSet&,
     keymaster::AuthorizationSet*) const override;
 keymaster::Buffer GenerateUniqueId(uint64_t, const keymaster_blob_t&, bool,
                                    keymaster_error_t*) const override;
 const VerifiedBootParams* GetVerifiedBootParams(
     keymaster_error_t* error) const override;
 keymaster::KeymasterKeyBlob GetAttestationKey(
     keymaster_algorithm_t algorithm, keymaster_error_t* error) const override;
 keymaster::CertificateChain GetAttestationChain(
     keymaster_algorithm_t algorithm, keymaster_error_t* error) const override;

private:
    mutable std::unique_ptr<VerifiedBootParams> vb_params_;
};
