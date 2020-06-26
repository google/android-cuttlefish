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

#include <tss2/tss2_esys.h>

#include "host/commands/secure_env/tpm_auth.h"

class TpmResourceManager;

struct EsysDeleter {
  void operator()(void* data) { Esys_Free(data); }
};

template<typename T>
using UniqueEsysPtr = std::unique_ptr<T, EsysDeleter>;

/**
 * Returns a HMAC signature for `data` with the key loaded into the TPM at
 * `key_handle`.
 *
 * The signature is a byte string that certifies a process that can make TPM
 * API calls has signed off on using another byte string (`data`) for some
 * purpose, which is implicitly tied to the signing key. In this case, the
 * secure_env process is the only process that should have TPM access.
 * secure_env can then transmit some data together with a signature over that
 * data, an external system (Android) can hold onto this data and the signature,
 * and then the secure_env process can receive the data back. The signature
 * is used to check that the data has not been tampered with.
 */
UniqueEsysPtr<TPM2B_DIGEST> TpmHmac(
    TpmResourceManager* resource_manager,
    ESYS_TR key_handle,
    TpmAuth auth,
    const uint8_t* data,
    size_t data_size);
