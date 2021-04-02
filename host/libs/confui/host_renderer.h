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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <freetype/ftglyph.h>  // $(croot)/external/freetype
#include <teeui/utils.h>       // $(croot)/system/teeui/libteeui/.../include

#include "common/libs/confui/confui.h"

#include "host/libs/confui/server_common.h"
#include "host/libs/screen_connector/screen_connector.h"

namespace cuttlefish {
namespace confui {

/**
 * create a raw frame for confirmation UI dialog
 */
class ConfUiRenderer {
 public:
  ConfUiRenderer(const std::uint32_t) {}

  std::tuple<TeeUiFrame&, bool> RenderRawFrame(
      const std::string& confirmation_msg, const std::string& lang_id) {
    // TODO(kwstephenkim@google.com): replace with real implementation
    prompt_ = confirmation_msg;  // placeholder assignment for compile
    lang_id_ = lang_id;
    return {empty_frame_, false};
  }

  bool IsFrameReady() const { return false; }
  // to make compiler happy until replaced by real implementation
  TeeUiFrame empty_frame_;
  std::string prompt_;
  std::string lang_id_;
};
}  // end of namespace confui
}  // end of namespace cuttlefish
