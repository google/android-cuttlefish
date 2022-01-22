/*
 * Copyright (C) 2021 The Android Open Source Project
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

typedef uint8_t nfc_event_t;
typedef uint8_t nfc_status_t;

/*
 * The callback passed in from the NFC stack that the HAL
 * can use to pass events back to the stack.
 */
typedef void(nfc_stack_callback_t)(nfc_event_t event,
                                   nfc_status_t event_status);

/*
 * The callback passed in from the NFC stack that the HAL
 * can use to pass incomming data to the stack.
 */
typedef void(nfc_stack_data_callback_t)(uint16_t data_len, uint8_t* p_data);

enum {
  HAL_NFC_OPEN_CPLT_EVT = 0u,
  HAL_NFC_CLOSE_CPLT_EVT = 1u,
  HAL_NFC_POST_INIT_CPLT_EVT = 2u,
  HAL_NFC_PRE_DISCOVER_CPLT_EVT = 3u,
  HAL_NFC_REQUEST_CONTROL_EVT = 4u,
  HAL_NFC_RELEASE_CONTROL_EVT = 5u,
  HAL_NFC_ERROR_EVT = 6u,
  HAL_HCI_NETWORK_RESET = 7u,
};

enum {
  HAL_NFC_STATUS_OK = 0u,
  HAL_NFC_STATUS_FAILED = 1u,
  HAL_NFC_STATUS_ERR_TRANSPORT = 2u,
  HAL_NFC_STATUS_ERR_CMD_TIMEOUT = 3u,
  HAL_NFC_STATUS_REFUSED = 4u,
};
