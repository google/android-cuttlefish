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

#include <iostream>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "host/commands/cvd/frontline_parser.h"

namespace std {

template <typename T>
static std::ostream& operator<<(std::ostream& out, const std::vector<T>& v) {
  if (v.empty()) {
    out << "{}";
    return out;
  }
  if (v.size() == 1) {
    out << "{" << v.front() << "}";
    return out;
  }
  out << "{";
  for (size_t i = 0; i != v.size() - 1; i++) {
    out << v.at(i) << ", ";
  }
  out << v.back() << "}";
  return out;
}

}  // namespace std

namespace cuttlefish {

TEST(FrontlineParserTest, CvdOnly) {
  cvd_common::Args input{"cvd"};
  FlagCollection empty_flags;
  FrontlineParser::ParserParam parser_param{.server_supported_subcmds = {},
                                            .internal_cmds = {},
                                            .all_args = {"cvd"},
                                            .cvd_flags = empty_flags};

  auto result = FrontlineParser::Parse(parser_param);

  ASSERT_TRUE(result.ok()) << result.error().Trace();
  auto& parser_ptr = *result;
  ASSERT_TRUE(parser_ptr);
  ASSERT_EQ("cvd", parser_ptr->ProgPath());
  ASSERT_EQ(std::nullopt, parser_ptr->SubCmd())
      << (parser_ptr->SubCmd() ? std::string("nullopt")
                               : *parser_ptr->SubCmd());
  ASSERT_EQ(cvd_common::Args{}, parser_ptr->SubCmdArgs());
  ASSERT_EQ(cvd_common::Args{}, parser_ptr->CvdArgs());
}

}  // namespace cuttlefish
