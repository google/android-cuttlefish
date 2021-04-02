//
// Copyright (C) 2021 The Android Open Source Project
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

/**
 * confirmation UI host and guest common library
 *
 * This header should be included by other components outside
 * common/lib/confui. Those inside should use each individual
 * header file(s)
 *
 */
#include "common/libs/confui/packet.h"
#include "common/libs/confui/protocol.h"
#include "common/libs/confui/utils.h"

namespace cuttlefish {
namespace confui {
using packet::RecvConfUiMsg;
using packet::SendAck;
using packet::SendCmd;
using packet::SendResponse;
}  // end of namespace confui
}  // end of namespace cuttlefish
