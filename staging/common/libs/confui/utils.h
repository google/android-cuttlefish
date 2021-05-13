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

#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <android-base/logging.h>

namespace cuttlefish {
template <typename T>
constexpr typename std::underlying_type_t<T> Enum2Base(T t) {
  return static_cast<typename std::underlying_type_t<T>>(t);
}
}  // end of namespace cuttlefish

namespace cuttlefish {
namespace confui {
template <typename Delim, typename... Args>
std::string ArgsToStringWithDelim(Delim&& delim, Args&&... args) {
  std::stringstream ss;
  ([&ss, &delim](auto& arg) { ss << arg << delim; }(args), ...);
  return ss.str();
}

// make t... to a single string with no blank in between
template <typename... Args>
std::string ArgsToString(Args&&... args) {
  return ArgsToStringWithDelim("", std::forward<Args>(args)...);
}

// note that no () surrounding LOG(level) << "ConfUI:" is crucial
#define ConfUiLog(LOG_LEVEL) LOG(LOG_LEVEL) << "ConfUI: "

// TODO(kwstephenkim@google.com): make these look more like LOG(level)
#define ConfUiCheck(cond) CHECK(cond) << "ConfUI: "

}  // end of namespace confui
}  // end of namespace cuttlefish
