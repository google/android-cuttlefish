//
// Copyright (C) 2024 The Android Open Source Project
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

#include <optional>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include <fmt/format.h>

#ifndef CONST_IF_LATER_FMTLIB
#if FMT_VERSION >= 90000
#define CONST_IF_LATER_FMTLIB const
#else
#define CONST_IF_LATER_FMTLIB
#endif
#endif
#include "common/libs/utils/flag_parser.h"

namespace cuttlefish {

struct ChromeOsBuilder {
  std::string project;
  std::string bucket;
  std::string builder;
};
std::ostream& operator<<(std::ostream&, const ChromeOsBuilder&);

using ChromeOsBuildString = std::variant<ChromeOsBuilder, std::string>;
std::ostream& operator<<(std::ostream&, const ChromeOsBuildString&);
std::ostream& operator<<(std::ostream&,
                         const std::optional<ChromeOsBuildString>&);

Flag GflagsCompatFlag(const std::string& name,
                      std::vector<std::optional<ChromeOsBuildString>>& value);

}  // namespace cuttlefish

template <>
struct fmt::formatter<cuttlefish::ChromeOsBuilder>
    : formatter<std::string_view> {
  template <typename FormatContext>
  auto format(const cuttlefish::ChromeOsBuilder& cob,
              FormatContext& ctx) CONST_IF_LATER_FMTLIB {
    auto formatted =
        fmt::format("{}/{}/{}", cob.project, cob.bucket, cob.builder);
    return formatter<std::string_view>::format(formatted, ctx);
  }
};

template <>
struct fmt::formatter<cuttlefish::ChromeOsBuildString>
    : formatter<std::string_view> {
  template <typename FormatContext>
  auto format(const cuttlefish::ChromeOsBuildString& cobs,
              FormatContext& ctx) CONST_IF_LATER_FMTLIB {
    auto formatted =
        std::visit([](auto&& value) { return fmt::format("{}", value); }, cobs);
    return formatter<std::string_view>::format(formatted, ctx);
  }
};

template <>
struct fmt::formatter<std::optional<cuttlefish::ChromeOsBuildString>>
    : formatter<std::string_view> {
  template <typename FormatContext>
  auto format(const std::optional<cuttlefish::ChromeOsBuildString>& cobs,
              FormatContext& ctx) CONST_IF_LATER_FMTLIB {
    auto formatted = cobs ? fmt::format("{}", *cobs) : "";
    return formatter<std::string_view>::format(formatted, ctx);
  }
};
