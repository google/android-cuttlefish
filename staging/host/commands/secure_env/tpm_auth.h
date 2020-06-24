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

#include <tss2/tss2_esys.h>

/**
 * Authorization wrapper for TPM2 calls.
 *
 * Most methods in tss2_esys.h take 3 ESYS_TR values for sessions and
 * authorization, with constraints that unused authorizations are all
 * ESYS_TR_NONE and are all at the end.
 *
 * This class is a convenience for specifying between 1 and 3
 * authorizations concisely and enforcing that the constraints are met.
 */
class TpmAuth {
public:
  TpmAuth(ESYS_TR auth1);
  TpmAuth(ESYS_TR auth1, ESYS_TR auth2);
  TpmAuth(ESYS_TR auth1, ESYS_TR auth2, ESYS_TR auth3);

  ESYS_TR auth1() const;
  ESYS_TR auth2() const;
  ESYS_TR auth3() const;
private:
  ESYS_TR auth1_;
  ESYS_TR auth2_;
  ESYS_TR auth3_;
};
