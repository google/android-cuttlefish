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

#include "host/commands/secure_env/test_tpm.h"

#include <android-base/logging.h>
#include <tss2/tss2_rc.h>

namespace cuttlefish {

TestTpm::TestTpm() {
  auto rc = Esys_Initialize(&esys_, tpm_.TctiContext(), nullptr);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(FATAL) << "Could not initialize esys: " << Tss2_RC_Decode(rc) << " ("
               << rc << ")";
  }
}

TestTpm::~TestTpm() { Esys_Finalize(&esys_); }

ESYS_CONTEXT* TestTpm::Esys() { return esys_; }

}  // namespace cuttlefish
