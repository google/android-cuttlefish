/*
 * Copyright (C) 2023 The Android Open Source Project
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
namespace cuttlefish {

// TODO(moelsherif) :review this value once we have a better idea of the size of
// the messages
const uint32_t MAX_MSG_SIZE = 200;

typedef struct msg_buffer {
  long mesg_type;
  char mesg_text[MAX_MSG_SIZE];
} msg_buffer;

constexpr char kCfMetricsQueueName[] = "cf_metrics_msg_queue";

}  // namespace cuttlefish
