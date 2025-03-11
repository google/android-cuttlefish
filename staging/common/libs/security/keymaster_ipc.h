/*
 * Copyright (C) 2012 The Android Open Source Project
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

// clang-format off

// Commands
enum keymaster_command : uint32_t {
    KEYMASTER_RESP_BIT              = 1,
    KEYMASTER_STOP_BIT              = 2,
    KEYMASTER_REQ_SHIFT             = 2,

    KM_GENERATE_KEY                 = (0 << KEYMASTER_REQ_SHIFT),
    KM_BEGIN_OPERATION              = (1 << KEYMASTER_REQ_SHIFT),
    KM_UPDATE_OPERATION             = (2 << KEYMASTER_REQ_SHIFT),
    KM_FINISH_OPERATION             = (3 << KEYMASTER_REQ_SHIFT),
    KM_ABORT_OPERATION              = (4 << KEYMASTER_REQ_SHIFT),
    KM_IMPORT_KEY                   = (5 << KEYMASTER_REQ_SHIFT),
    KM_EXPORT_KEY                   = (6 << KEYMASTER_REQ_SHIFT),
    KM_GET_VERSION                  = (7 << KEYMASTER_REQ_SHIFT),
    KM_ADD_RNG_ENTROPY              = (8 << KEYMASTER_REQ_SHIFT),
    KM_GET_SUPPORTED_ALGORITHMS     = (9 << KEYMASTER_REQ_SHIFT),
    KM_GET_SUPPORTED_BLOCK_MODES    = (10 << KEYMASTER_REQ_SHIFT),
    KM_GET_SUPPORTED_PADDING_MODES  = (11 << KEYMASTER_REQ_SHIFT),
    KM_GET_SUPPORTED_DIGESTS        = (12 << KEYMASTER_REQ_SHIFT),
    KM_GET_SUPPORTED_IMPORT_FORMATS = (13 << KEYMASTER_REQ_SHIFT),
    KM_GET_SUPPORTED_EXPORT_FORMATS = (14 << KEYMASTER_REQ_SHIFT),
    KM_GET_KEY_CHARACTERISTICS      = (15 << KEYMASTER_REQ_SHIFT),
    KM_ATTEST_KEY                   = (16 << KEYMASTER_REQ_SHIFT),
    KM_UPGRADE_KEY                  = (17 << KEYMASTER_REQ_SHIFT),
    KM_CONFIGURE                    = (18 << KEYMASTER_REQ_SHIFT),
    KM_GET_HMAC_SHARING_PARAMETERS  = (19 << KEYMASTER_REQ_SHIFT),
    KM_COMPUTE_SHARED_HMAC          = (20 << KEYMASTER_REQ_SHIFT),
    KM_VERIFY_AUTHORIZATION         = (21 << KEYMASTER_REQ_SHIFT),
    KM_DELETE_KEY                   = (22 << KEYMASTER_REQ_SHIFT),
    KM_DELETE_ALL_KEYS              = (23 << KEYMASTER_REQ_SHIFT),
    KM_DESTROY_ATTESTATION_IDS      = (24 << KEYMASTER_REQ_SHIFT),
    KM_IMPORT_WRAPPED_KEY           = (25 << KEYMASTER_REQ_SHIFT),
};

/**
 * keymaster_message - Serial header for communicating with KM server
 * @cmd: the command, one of keymaster_command.
 * @payload: start of the serialized command specific payload
 */
struct keymaster_message {
    keymaster_command cmd;
    uint32_t payload_size;
    uint8_t payload[0];
};
