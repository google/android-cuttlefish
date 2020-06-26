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

#include "in_process_tpm.h"

#include <endian.h>
#include <stddef.h>

#include <tss2/tss2_rc.h>

#include "host/commands/secure_env/tpm_commands.h"

extern "C" {
typedef int SOCKET;
#include "TpmBuildSwitches.h"
#include "TpmTcpProtocol.h"
#include "Simulator_fp.h"
#include "Manufacture_fp.h"
#define delete delete_
#include "Platform_fp.h"
#undef delete
#undef DEBUG
}

#include <android-base/logging.h>

struct __attribute__((__packed__)) tpm_message_header {
  uint16_t tag;
  uint32_t length;
  uint32_t ordinal;
};

InProcessTpm* InProcessTpm::TpmFromContext(TSS2_TCTI_CONTEXT* context) {
  auto offset = offsetof(InProcessTpm, tcti_context_);
  char* context_char = reinterpret_cast<char*>(context);
  return reinterpret_cast<InProcessTpm*>(context_char - offset);
}

static TSS2_RC Transmit(
    TSS2_TCTI_CONTEXT *context, size_t size, uint8_t const *command) {
  return InProcessTpm::TpmFromContext(context)->Transmit(size, command);
}

static TSS2_RC Receive(
    TSS2_TCTI_CONTEXT *context,
    size_t* size,
    uint8_t* response,
    int32_t timeout) {
  return
      InProcessTpm::TpmFromContext(context)->Receive(size, response, timeout);
}

InProcessTpm::InProcessTpm() {
  tcti_context_.v1.magic = 0xFAD;
  tcti_context_.v1.version = 1;
  tcti_context_.v1.transmit = ::Transmit;
  tcti_context_.v1.receive = ::Receive;
  _plat__NVEnable(NULL);
  if (_plat__NVNeedsManufacture()) {
    // Can't use android logging here due to a macro conflict with TPM internals
    LOG(DEBUG) << "Manufacturing TPM state";
    if (TPM_Manufacture(1)) {
      LOG(FATAL) << "Failed to manufacture TPM state";
    }
  }
  _rpc__Signal_PowerOn(false);
  _rpc__Signal_NvOn();
}

InProcessTpm::~InProcessTpm() {
  _rpc__Signal_NvOff();
  _rpc__Signal_PowerOff();
}

TSS2_TCTI_CONTEXT* InProcessTpm::TctiContext() {
  return reinterpret_cast<TSS2_TCTI_CONTEXT*>(&tcti_context_);
}

TSS2_RC InProcessTpm::Transmit(size_t size, uint8_t const* command) {
  std::lock_guard lock(queue_mutex_);
  command_queue_.emplace_back(command, command + size);
  return TSS2_RC_SUCCESS;
}

TSS2_RC InProcessTpm::Receive(size_t* size, uint8_t* response, int32_t) {
  // TODO(schuffelen): Use the timeout argument
  std::vector<uint8_t> request;
  {
    std::lock_guard lock(queue_mutex_);
    if (command_queue_.empty()) {
      return TSS2_TCTI_RC_GENERAL_FAILURE;
    }
    request = std::move(command_queue_.front());
    command_queue_.pop_front();
  }
  auto header = reinterpret_cast<tpm_message_header*>(request.data());
  LOG(VERBOSE) << "Sending TPM command "
               << TpmCommandName(be32toh(header->ordinal));
  _IN_BUFFER input = {
      .BufferSize = request.size(),
      .Buffer = request.data(),
  };
  _OUT_BUFFER output = {
      .BufferSize = (uint32_t) *size,
      .Buffer = response,
  };
  _rpc__Send_Command(3, input, &output);
  *size = output.BufferSize;
  header = reinterpret_cast<tpm_message_header*>(response);
  auto rc = be32toh(header->ordinal);
  LOG(VERBOSE) << "Received TPM response " << Tss2_RC_Decode(rc)
               << " (" << rc << ")";
  return TSS2_RC_SUCCESS;
}
