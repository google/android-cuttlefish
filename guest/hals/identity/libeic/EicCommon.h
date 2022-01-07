/*
 * Copyright 2020, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HARDWARE_IDENTITY_EIC_COMMON_H
#define ANDROID_HARDWARE_IDENTITY_EIC_COMMON_H

// Feature version 202009:
//
//         CredentialKeys = [
//              bstr,   ; storageKey, a 128-bit AES key
//              bstr,   ; credentialPrivKey, the private key for credentialKey
//         ]
//
// Feature version 202101:
//
//         CredentialKeys = [
//              bstr,   ; storageKey, a 128-bit AES key
//              bstr,   ; credentialPrivKey, the private key for credentialKey
//              bstr    ; proofOfProvisioning SHA-256
//         ]
//
// where storageKey is 16 bytes, credentialPrivateKey is 32 bytes, and
// proofOfProvisioning SHA-256 is 32 bytes.
#define EIC_CREDENTIAL_KEYS_CBOR_SIZE_FEATURE_VERSION_202009 52
#define EIC_CREDENTIAL_KEYS_CBOR_SIZE_FEATURE_VERSION_202101 86

#endif  // ANDROID_HARDWARE_IDENTITY_EIC_COMMON_H
