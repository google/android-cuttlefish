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

#include <keymaster/android_keymaster_utils.h>
#include <tss2/tss2_esys.h>

#include "host/commands/secure_env/tpm_auth.h"

/**
 * Encrypt `data_in` to `data_out`, which are both buffers of size `data_size`.
 *
 * There are no integrity guarantees on this data: if the encrypted data is
 * corrupted, decrypting it could either fail or produce corrupted output.
 */
bool TpmEncrypt(
    ESYS_CONTEXT* esys,
    ESYS_TR key_handle,
    TpmAuth auth,
    uint8_t* data_in,
    uint8_t* data_out,
    size_t data_size);


/**
 * Decrypt `data_in` to `data_out`, which are both buffers of size `data_size`.
 *
 * There are no integrity guarantees on this data: if the encrypted data is
 * corrupted, decrypting it could either fail or produce corrupted output.
 */
bool TpmDecrypt(
    ESYS_CONTEXT* esys,
    ESYS_TR key_handle,
    TpmAuth auth,
    uint8_t* data_in,
    uint8_t* data_out,
    size_t data_size);
