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

#include "tpm_auth.h"

#include <tuple>

TpmAuth::TpmAuth(ESYS_TR auth): TpmAuth(auth, ESYS_TR_NONE, ESYS_TR_NONE) {}
TpmAuth::TpmAuth(ESYS_TR auth1, ESYS_TR auth2)
    : TpmAuth(auth1, auth2, ESYS_TR_NONE) {}
TpmAuth::TpmAuth(ESYS_TR auth1, ESYS_TR auth2, ESYS_TR auth3) {
  if (auth2 == ESYS_TR_NONE && auth3 != ESYS_TR_NONE) {
    std::swap(auth2, auth3);
  }
  if (auth1 == ESYS_TR_NONE && auth2 != ESYS_TR_NONE) {
    std::swap(auth1, auth2);
  }
  std::tie(auth1_, auth2_, auth3_) = std::make_tuple(auth1, auth2, auth3);
}

ESYS_TR TpmAuth::auth1() const {
  return auth1_;
}

ESYS_TR TpmAuth::auth2() const {
  return auth2_;
}

ESYS_TR TpmAuth::auth3() const {
  return auth3_;
}
