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

#include <stddef.h>

#include <tss2/tss2_esys.h>
#include <tss2/tss2_rc.h>

#include <android-base/endian.h>

#include "host/commands/secure_env/tpm_commands.h"

extern "C" {
#ifndef _WIN32
typedef int SOCKET;
#endif
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

#include <mutex>

namespace cuttlefish {

struct __attribute__((__packed__)) tpm_message_header {
  uint16_t tag;
  uint32_t length;
  uint32_t ordinal;
};

class InProcessTpm::Impl {
 public:
  static Impl* FromContext(TSS2_TCTI_CONTEXT* context) {
    auto offset = offsetof(Impl, tcti_context_);
    char* context_char = reinterpret_cast<char*>(context);
    return reinterpret_cast<Impl*>(context_char - offset);
  }

  static TSS2_RC Transmit(
      TSS2_TCTI_CONTEXT *context, size_t size, uint8_t const *command) {
    auto impl = FromContext(context);
    std::lock_guard lock(impl->queue_mutex_);
    impl->command_queue_.emplace_back(command, command + size);
    return TSS2_RC_SUCCESS;
  }

  static TSS2_RC Receive(
      TSS2_TCTI_CONTEXT *context,
      size_t* size,
      uint8_t* response,
      int32_t /* timeout */) {
    auto impl = FromContext(context);
    // TODO(schuffelen): Use the timeout argument
    std::vector<uint8_t> request;
    {
      std::lock_guard lock(impl->queue_mutex_);
      if (impl->command_queue_.empty()) {
        return TSS2_TCTI_RC_GENERAL_FAILURE;
      }
      request = std::move(impl->command_queue_.front());
      impl->command_queue_.pop_front();
    }
    auto header = reinterpret_cast<tpm_message_header*>(request.data());
    LOG(VERBOSE) << "Sending TPM command "
                << TpmCommandName(be32toh(header->ordinal));
    _IN_BUFFER input = {
        .BufferSize = static_cast<unsigned long>(request.size()),
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

  Impl() {
    {
      std::lock_guard<std::mutex> lock(global_mutex);
      // This is a limitation of ms-tpm-20-ref
      CHECK(!global_instance) << "InProcessTpm internally uses global data, so "
                              << "only one can exist.";
      global_instance = this;
    }

    tcti_context_.v1.magic = 0xFAD;
    tcti_context_.v1.version = 1;
    tcti_context_.v1.transmit = Impl::Transmit;
    tcti_context_.v1.receive = Impl::Receive;
    _plat__NVEnable(NULL);
    if (_plat__NVNeedsManufacture()) {
      // Can't use android logging here due to a macro conflict with TPM
      // internals
      LOG(DEBUG) << "Manufacturing TPM state";
      if (TPM_Manufacture(1)) {
        LOG(FATAL) << "Failed to manufacture TPM state";
      }
    }
    _rpc__Signal_PowerOn(false);
    _rpc__Signal_NvOn();

    ESYS_CONTEXT* esys = nullptr;
    auto rc = Esys_Initialize(&esys, TctiContext(), nullptr);
    if (rc != TPM2_RC_SUCCESS) {
      LOG(FATAL) << "Could not initialize esys: " << Tss2_RC_Decode(rc) << " ("
                 << rc << ")";
    }

    rc = Esys_Startup(esys, TPM2_SU_CLEAR);
    if (rc != TPM2_RC_SUCCESS) {
      LOG(FATAL) << "TPM2_Startup failed: " << Tss2_RC_Decode(rc) << " (" << rc
                 << ")";
    }

    TPM2B_AUTH auth = {};
    Esys_TR_SetAuth(esys, ESYS_TR_RH_LOCKOUT, &auth);

    rc = Esys_DictionaryAttackLockReset(
        /* esysContext */ esys,
        /* lockHandle */ ESYS_TR_RH_LOCKOUT,
        /* shandle1 */ ESYS_TR_PASSWORD,
        /* shandle2 */ ESYS_TR_NONE,
        /* shandle3 */ ESYS_TR_NONE);

    if (rc != TPM2_RC_SUCCESS) {
      LOG(FATAL) << "Could not reset TPM lockout: " << Tss2_RC_Decode(rc)
                 << " (" << rc << ")";
    }

    Esys_Finalize(&esys);
  }

  ~Impl() {
    _rpc__Signal_NvOff();
    _rpc__Signal_PowerOff();
    std::lock_guard<std::mutex> lock(global_mutex);
    global_instance = nullptr;
  }

  TSS2_TCTI_CONTEXT* TctiContext() {
    return reinterpret_cast<TSS2_TCTI_CONTEXT*>(&tcti_context_);
  }

 private:
  static std::mutex global_mutex;
  static Impl* global_instance;
  TSS2_TCTI_CONTEXT_COMMON_CURRENT tcti_context_;
  std::list<std::vector<uint8_t>> command_queue_;
  std::mutex queue_mutex_;
};

std::mutex InProcessTpm::Impl::global_mutex;
InProcessTpm::Impl* InProcessTpm::Impl::global_instance;

InProcessTpm::InProcessTpm() : impl_(new Impl()) {}

InProcessTpm::~InProcessTpm() = default;

TSS2_TCTI_CONTEXT* InProcessTpm::TctiContext() { return impl_->TctiContext(); }

}  // namespace cuttlefish
