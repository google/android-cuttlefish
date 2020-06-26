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

#include <list>
#include <mutex>
#include <vector>

#include <tss2/tss2_tcti.h>

/*
 * Exposes a TSS2_TCTI_CONTEXT for interacting with an in-process TPM simulator.
 *
 * TSS2_TCTI_CONTEXT is the abstraction for "communication channel to a TPM".
 * It is not safe to create more than one of these per process or per working
 * directory, as the TPM simulator implementation relies heavily on global
 * variables and files saved in the working directory.
 *
 * TODO(schuffelen): Consider moving this to a separate process with its own
 * working directory.
 */
class InProcessTpm {
public:
  InProcessTpm();
  ~InProcessTpm();

  TSS2_RC Transmit(size_t size, uint8_t const* command);
  TSS2_RC Receive(size_t* size, uint8_t* response, int32_t timeout);

  TSS2_TCTI_CONTEXT* TctiContext();
  static InProcessTpm* TpmFromContext(TSS2_TCTI_CONTEXT*);
private:
  TSS2_TCTI_CONTEXT_COMMON_CURRENT tcti_context_;
  std::list<std::vector<uint8_t>> command_queue_;
  std::mutex queue_mutex_;
};
