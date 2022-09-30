//
// Copyright (C) 2022 The Android Open Source Project
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

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <gtest/gtest.h>

#include "host/commands/cvd/selector/selector_cmdline_parser.h"

using Args = std::vector<std::string>;

void SingleRun(const Args& args, const Args& pre_ref, const Args& selector_ref,
               const Args& after_ref) {
  auto result = cuttlefish::selector::SeparateArguments(args);
  ASSERT_TRUE(result.ok());
  auto [pre, selector, after] = *result;
  ASSERT_EQ(pre, pre_ref);
  ASSERT_EQ(selector, selector_ref);
  ASSERT_EQ(after, after_ref);
}

TEST(CvdSelectorCmdlineTest, BlanksInSelectorOptions) {
  std::vector<std::string> args_strings = {
      "cvd [--name=start --instance_id 7 ] --nohelp --daemon=no",
      "cvd [ --name=start --instance_id 7] --nohelp --daemon=no",
      "cvd [--name=start --instance_id 7] --nohelp --daemon=no",
      "cvd [ --name=start --instance_id 7 ] --nohelp --daemon=no"};

  std::vector<Args> args_list;
  for (const auto& args_string : args_strings) {
    auto tokens = android::base::Tokenize(args_string, " ");
    args_list.emplace_back(std::move(tokens));
  }

  Args pre_ref{"cvd"};
  Args selector_ref{"--name=start", "--instance_id", "7"};
  Args after_ref{"--nohelp", "--daemon=no"};

  for (const auto& args : args_list) {
    SingleRun(args, pre_ref, selector_ref, after_ref);
  }
}

TEST(CvdSelectorCmdlineTest, NoSelectorOption) {
  std::vector<Args> args_list = {{"cvd", "[]", "--nohelp", "--daemon=no"},
                                 {"cvd", "[", "]", "--nohelp", "--daemon=no"}};
  Args pre_ref{"cvd"};
  Args selector_ref;
  Args after_ref{"--nohelp", "--daemon=no"};

  for (const auto& args : args_list) {
    SingleRun(args, pre_ref, selector_ref, after_ref);
  }

  SingleRun({"cvd", "--nohelp", "--daemon=no"},
            {"cvd", "--nohelp", "--daemon=no"}, {}, {});
}

TEST(CvdSelectorCmdlineTest, NoOption) {
  Args args{"cvd"};
  Args pre_ref{"cvd"};
  Args selector_ref;
  Args after_ref;
  SingleRun(args, pre_ref, selector_ref, after_ref);
}

TEST(CvdSelectorCmdlineTest, NoProgramPath) {
  std::vector<std::string> args_strings = {
      "[--name=start --instance_id 7 ] --nohelp --daemon=no",
      "[ --name=start --instance_id 7] --nohelp --daemon=no",
      "[--name=start --instance_id 7] --nohelp --daemon=no",
      "[ --name=start --instance_id 7 ] --nohelp --daemon=no"};

  std::vector<Args> args_list;
  for (const auto& args_string : args_strings) {
    auto tokens = android::base::Tokenize(args_string, " ");
    args_list.emplace_back(std::move(tokens));
  }

  Args pre_ref;
  Args selector_ref{"--name=start", "--instance_id", "7"};
  Args after_ref{"--nohelp", "--daemon=no"};

  for (const auto& args : args_list) {
    SingleRun(args, pre_ref, selector_ref, after_ref);
  }
}
