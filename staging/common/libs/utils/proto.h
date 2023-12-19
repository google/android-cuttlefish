//
// Copyright (C) 2023 The Android Open Source Project
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

#include <type_traits>
#include <vector>

#include <fmt/core.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

/* Format proto messages as textproto with {} or {:t} and json with {:j}. */
template <typename T>
struct fmt::formatter<
    T,
    std::enable_if_t<std::is_base_of_v<google::protobuf::Message, T>, char>> {
 public:
  constexpr format_parse_context::iterator parse(format_parse_context& ctx) {
    auto it = ctx.begin();
    for (; it != ctx.end() && *it != '}'; it++) {
      format_ = *it;
    }
    return it;
  }
  format_context::iterator format(const T& proto, format_context& ctx) const {
    std::string text;
    if (format_ == 't') {
      if (!google::protobuf::TextFormat::PrintToString(proto, &text)) {
        return fmt::format_to(ctx.out(), "(proto format error");
      }
    } else if (format_ == 'j') {
      auto result = google::protobuf::util::MessageToJsonString(proto, &text);
      if (!result.ok()) {
        return fmt::format_to(ctx.out(), "(json error: {})",
                              result.message().ToString());
      }
    } else {
      return fmt::format_to(ctx.out(), "(unknown format specifier)");
    }
    return fmt::format_to(ctx.out(), "{}", text);
  }

 private:
  char format_ = 't';
};
