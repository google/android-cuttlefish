//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/gflags_xml_parser.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;

TEST(GflagsXmlParserTest, ParseValidXml) {
  std::string xml = R"(<?xml version="1.0"?>
<AllFlags>
  <program>some_program_name</program>
  <usage>Usage string, may contain a warning and parenthesis()</usage>
  <flag>
    <file>path/to/file.cpp</file>
    <name>flag1</name>
    <meaning>meaning1</meaning>
    <default>def1</default>
    <current>curr1</current>
    <type>string</type>
  </flag>
  <flag>
    <file>path/to/file2.cpp</file>
    <name>flag2</name>
    <meaning>meaning2</meaning>
    <default>def2</default>
    <current>curr2</current>
    <type>int32</type>
  </flag>
</AllFlags>)";

  auto result = ParseGflagsXmlHelp(xml);
  ASSERT_THAT(result, IsOk());
  std::vector<GflagDescription> expected = {{.file = "path/to/file.cpp",
                                             .name = "flag1",
                                             .meaning = "meaning1",
                                             .default_value = "def1",
                                             .current_value = "curr1",
                                             .type = "string"},
                                            {.file = "path/to/file2.cpp",
                                             .name = "flag2",
                                             .meaning = "meaning2",
                                             .default_value = "def2",
                                             .current_value = "curr2",
                                             .type = "int32"}};
  EXPECT_EQ(*result, expected);
}

TEST(GflagsXmlParserTest, ParseEmptyFlags) {
  std::string xml = R"(<?xml version="1.0"?>
<AllFlags>
  <program>cvd_internal_start</program>
  <usage>Warning: SetUsageMessage() never called</usage>
</AllFlags>)";

  auto result = ParseGflagsXmlHelp(xml);
  ASSERT_THAT(result, IsOk());
  EXPECT_TRUE(result->empty());
}

TEST(GflagsXmlParserTest, ParseInvalidXml) {
  std::string xml = "not xml";
  auto result = ParseGflagsXmlHelp(xml);
  EXPECT_THAT(result, IsError());
}

TEST(GflagsXmlParserTest, ParseWrongRoot) {
  std::string xml = R"(<?xml version="1.0"?><WrongRoot></WrongRoot>)";
  auto result = ParseGflagsXmlHelp(xml);
  EXPECT_THAT(result, IsError());
}

}  // namespace
}  // namespace cuttlefish
