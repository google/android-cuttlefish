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

#include "host/commands/secure_env/device_tpm.h"

#include <android-base/logging.h>
#include <tss2/tss2_rc.h>
#include <tss2/tss2_tcti.h>
#include <tss2/tss2_tcti_device.h>

namespace cuttlefish {

static void FinalizeTcti(TSS2_TCTI_CONTEXT* tcti_context) {
  if (tcti_context == nullptr) {
    return;
  }
  auto finalize_fn = TSS2_TCTI_FINALIZE(tcti_context);
  if (finalize_fn != nullptr) {
    finalize_fn(tcti_context);
  }
  delete[] (char*) tcti_context;
}

DeviceTpm::DeviceTpm(const std::string& path) : tpm_(nullptr, &FinalizeTcti) {
  size_t size = 0;
  auto rc = Tss2_Tcti_Device_Init(nullptr, &size, path.c_str());
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Could not get Device TCTI size: " << Tss2_RC_Decode(rc)
               << "(" << rc << ")";
    return;
  }
  tpm_.reset(reinterpret_cast<TSS2_TCTI_CONTEXT*>(new char[size]));
  rc = Tss2_Tcti_Device_Init(tpm_.get(), &size, path.c_str());
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Could not create Device TCTI: " << Tss2_RC_Decode(rc)
               << "(" << rc << ")";
    delete[] (char*) tpm_.release();
    return;
  }
}

TSS2_TCTI_CONTEXT* DeviceTpm::TctiContext() {
  return tpm_.get();
}

}  // namespace cuttlefish
