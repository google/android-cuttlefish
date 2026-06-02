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

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"

#include <stdint.h>

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include <libxml/parser.h>

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

TEST(FlagParser, DuplicateAlias) {
  FlagAlias alias = {FlagAliasMode::kFlagExact, "--flag"};
  ASSERT_DEATH({ Flag("flag").Alias(alias).Alias(alias); }, "Duplicate flag alias");
}

TEST(FlagParser, ConflictingAlias) {
  FlagAlias exact_alias = {FlagAliasMode::kFlagExact, "--flag"};
  FlagAlias following_alias = {FlagAliasMode::kFlagConsumesFollowing, "--flag"};
  ASSERT_DEATH({ Flag("flag").Alias(exact_alias).Alias(following_alias); },
               "Overlapping flag aliases");
}

TEST(FlagParser, StringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_THAT(flag.Parse({"-myflag=a"}), IsOk());
  ASSERT_EQ(value, "a");
  ASSERT_THAT(flag.Parse({"--myflag=b"}), IsOk());
  ASSERT_EQ(value, "b");
  ASSERT_THAT(flag.Parse({"-myflag", "c"}), IsOk());
  ASSERT_EQ(value, "c");
  ASSERT_THAT(flag.Parse({"--myflag", "d"}), IsOk());
  ASSERT_EQ(value, "d");
  ASSERT_THAT(flag.Parse({"--myflag="}), IsOk());
  ASSERT_EQ(value, "");
}

TEST(FlagParser, NormalizedStringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("my_flag", value);
  ASSERT_THAT(flag.Parse({"-my-flag=a"}), IsOk());
  ASSERT_EQ(value, "a");
  ASSERT_THAT(flag.Parse({"--my-flag=b"}), IsOk());
  ASSERT_EQ(value, "b");
  ASSERT_THAT(flag.Parse({"-my-flag", "c"}), IsOk());
  ASSERT_EQ(value, "c");
  ASSERT_THAT(flag.Parse({"--my-flag", "d"}), IsOk());
  ASSERT_EQ(value, "d");
  ASSERT_THAT(flag.Parse({"--my-flag="}), IsOk());
  ASSERT_EQ(value, "");
}

std::optional<std::map<std::string, std::string>> flagXml(const Flag& f) {
  std::stringstream xml_stream;
  if (!WriteGflagsCompatXml(f, xml_stream)) {
    return {};
  }
  auto xml = xml_stream.str();
  // Holds all memory for the parsed structure.
  std::unique_ptr<xmlDoc, void(*)(xmlDocPtr)> doc(
      xmlReadMemory(xml.c_str(), xml.size(), nullptr, nullptr, 0), xmlFreeDoc);
  if (!doc) {
    return {};
  }
  xmlNodePtr root_element = xmlDocGetRootElement(doc.get());
  std::map<std::string, std::string> elements_map;
  for (auto elem = root_element->children; elem != nullptr; elem = elem->next) {
    if (elem->type != xmlElementType::XML_ELEMENT_NODE) {
      continue;
    }
    elements_map[(char*)elem->name] = "";
    if (elem->children == nullptr) {
      continue;
    }
    if (elem->children->type != XML_TEXT_NODE) {
      continue;
    }
    elements_map[(char*)elem->name] = (char*)elem->children->content;
  }
  return elements_map;
}

TEST(FlagParser, GflagsIncompatibleFlag) {
  auto flag = Flag("flag").Alias({FlagAliasMode::kFlagExact, "--flag"});
  ASSERT_FALSE(flagXml(flag));
}

TEST(FlagParser, StringFlagXml) {
  std::string value = "somedefault";
  auto flag = GflagsCompatFlag("myflag", value).Help("somehelp");
  auto xml = flagXml(flag);
  ASSERT_TRUE(xml);
  ASSERT_NE((*xml)["file"], "");
  ASSERT_EQ((*xml)["name"], "myflag");
  ASSERT_EQ((*xml)["meaning"], "somehelp");
  ASSERT_EQ((*xml)["default"], "somedefault");
  ASSERT_EQ((*xml)["current"], "somedefault");
  ASSERT_EQ((*xml)["type"], "string");
}

TEST(FlagParser, RepeatedStringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_THAT(flag.Parse({"-myflag=a", "--myflag", "b"}), IsOk());
  ASSERT_EQ(value, "b");
}

TEST(FlagParser, RepeatedListFlag) {
  std::vector<std::string> elems;
  auto flag = GflagsCompatFlag("myflag");
  flag.Setter([&elems](const FlagMatch& match) -> Result<void> {
    elems.push_back(match.value);
    return {};
  });
  ASSERT_THAT(flag.Parse({"-myflag=a", "--myflag", "b"}), IsOk());
  ASSERT_EQ(elems, (std::vector<std::string>{"a", "b"}));
}

TEST(FlagParser, FlagRemoval) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  std::vector<std::string> flags = {"-myflag=a", "-otherflag=c"};
  ASSERT_THAT(flag.Parse(flags), IsOk());
  ASSERT_EQ(value, "a");
  ASSERT_EQ(flags, std::vector<std::string>{"-otherflag=c"});
  flags = {"-otherflag=a", "-myflag=c"};
  ASSERT_THAT(flag.Parse(flags), IsOk());
  ASSERT_EQ(value, "c");
  ASSERT_EQ(flags, std::vector<std::string>{"-otherflag=a"});
}

TEST(FlagParser, IntFlag) {
  int32_t value = 0;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_THAT(flag.Parse({"-myflag=5"}), IsOk());
  ASSERT_EQ(value, 5);
  ASSERT_THAT(flag.Parse({"--myflag=6"}), IsOk());
  ASSERT_EQ(value, 6);
  ASSERT_THAT(flag.Parse({"-myflag", "7"}), IsOk());
  ASSERT_EQ(value, 7);
  ASSERT_THAT(flag.Parse({"--myflag", "8"}), IsOk());
  ASSERT_EQ(value, 8);
}

TEST(FlagParser, IntFlagXml) {
  int value = 5;
  auto flag = GflagsCompatFlag("myflag", value).Help("somehelp");
  auto xml = flagXml(flag);
  ASSERT_TRUE(xml);
  ASSERT_NE((*xml)["file"], "");
  ASSERT_EQ((*xml)["name"], "myflag");
  ASSERT_EQ((*xml)["meaning"], "somehelp");
  ASSERT_EQ((*xml)["default"], "5");
  ASSERT_EQ((*xml)["current"], "5");
  ASSERT_EQ((*xml)["type"], "string");
}

TEST(FlagParser, BoolFlag) {
  bool value = false;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_THAT(flag.Parse({"-myflag"}), IsOk());
  ASSERT_TRUE(value);

  value = false;
  ASSERT_THAT(flag.Parse({"--myflag"}), IsOk());
  ASSERT_TRUE(value);

  value = false;
  ASSERT_THAT(flag.Parse({"-myflag=true"}), IsOk());
  ASSERT_TRUE(value);

  value = false;
  ASSERT_THAT(flag.Parse({"--myflag=true"}), IsOk());
  ASSERT_TRUE(value);

  value = true;
  ASSERT_THAT(flag.Parse({"-nomyflag"}), IsOk());
  ASSERT_FALSE(value);

  value = true;
  ASSERT_THAT(flag.Parse({"--nomyflag"}), IsOk());
  ASSERT_FALSE(value);

  value = true;
  ASSERT_THAT(flag.Parse({"-myflag=false"}), IsOk());
  ASSERT_FALSE(value);

  value = true;
  ASSERT_THAT(flag.Parse({"--myflag=false"}), IsOk());
  ASSERT_FALSE(value);

  ASSERT_THAT(flag.Parse({"--myflag=nonsense"}), IsError());
}

TEST(FlagParser, BoolFlagXml) {
  bool value = true;
  auto flag = GflagsCompatFlag("myflag", value).Help("somehelp");
  auto xml = flagXml(flag);
  ASSERT_TRUE(xml);
  ASSERT_NE((*xml)["file"], "");
  ASSERT_EQ((*xml)["name"], "myflag");
  ASSERT_EQ((*xml)["meaning"], "somehelp");
  ASSERT_EQ((*xml)["default"], "true");
  ASSERT_EQ((*xml)["current"], "true");
  ASSERT_EQ((*xml)["type"], "bool");
}

TEST(FlagParser, StringIntFlag) {
  int32_t int_value = 0;
  std::string string_value;
  auto int_flag = GflagsCompatFlag("int", int_value);
  auto string_flag = GflagsCompatFlag("string", string_value);
  std::vector<Flag> flags = {int_flag, string_flag};
  EXPECT_THAT(ConsumeFlags(flags, {"-int=5", "-string=a"}), IsOk());
  ASSERT_EQ(int_value, 5);
  ASSERT_EQ(string_value, "a");
  EXPECT_THAT(ConsumeFlags(flags, {"--int=6", "--string=b"}), IsOk());
  ASSERT_EQ(int_value, 6);
  ASSERT_EQ(string_value, "b");
  EXPECT_THAT(ConsumeFlags(flags, {"-int", "7", "-string", "c"}), IsOk());
  ASSERT_EQ(int_value, 7);
  ASSERT_EQ(string_value, "c");
  EXPECT_THAT(ConsumeFlags(flags, {"--int", "8", "--string", "d"}), IsOk());
  ASSERT_EQ(int_value, 8);
  ASSERT_EQ(string_value, "d");
}

TEST(FlagParser, StringVectorFlag) {
  std::vector<std::string> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag="}), IsOk());
  ASSERT_TRUE(value.empty());

  ASSERT_THAT(flag.Parse({"--myflag=foo"}), IsOk());
  ASSERT_EQ(value, std::vector<std::string>({"foo"}));

  ASSERT_THAT(flag.Parse({"--myflag=foo,bar"}), IsOk());
  ASSERT_EQ(value, std::vector<std::string>({"foo", "bar"}));

  ASSERT_THAT(flag.Parse({"--myflag=,bar"}), IsOk());
  ASSERT_EQ(value, std::vector<std::string>({"", "bar"}));

  ASSERT_THAT(flag.Parse({"--myflag=foo,"}), IsOk());
  ASSERT_EQ(value, std::vector<std::string>({"foo", ""}));

  ASSERT_THAT(flag.Parse({"--myflag=,"}), IsOk());
  ASSERT_EQ(value, std::vector<std::string>({"", ""}));
}

TEST(FlagParser, BoolVectorFlag) {
  std::vector<bool> value;
  bool default_value = true;
  auto flag = GflagsCompatFlag("myflag", value, default_value);
  ASSERT_THAT(flag.Parse({"--myflag="}), IsOk());
  ASSERT_TRUE(value.empty());
  ASSERT_THAT(flag.Parse({"--myflag=foo"}), IsError());
  ASSERT_TRUE(value.empty());
  ASSERT_THAT(flag.Parse({"--myflag=true,bar"}), IsError());
  ASSERT_TRUE(value.empty());

  ASSERT_THAT(flag.Parse({"--myflag=true"}), IsOk());
  ASSERT_EQ(value, std::vector<bool>({true}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true");

  ASSERT_THAT(flag.Parse({"--myflag=true,false"}), IsOk());
  ASSERT_EQ(value, std::vector<bool>({true, false}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true,false");

  ASSERT_THAT(flag.Parse({"--myflag=,false"}), IsOk());
  ASSERT_EQ(value, std::vector<bool>({true, false}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true,false");

  ASSERT_THAT(flag.Parse({"--myflag=true,"}), IsOk());
  ASSERT_EQ(value, std::vector<bool>({true, true}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true,true");

  ASSERT_THAT(flag.Parse({"--myflag=,"}), IsOk());
  ASSERT_EQ(value, std::vector<bool>({true, true}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true,true");
}

TEST(FlagParser, InvalidStringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_THAT(flag.Parse({"-myflag"}), IsError());
  ASSERT_THAT(flag.Parse({"--myflag"}), IsError());
}

TEST(FlagParser, InvalidIntFlag) {
  int value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_THAT(flag.Parse({"-myflag"}), IsError());
  ASSERT_THAT(flag.Parse({"--myflag"}), IsError());
  ASSERT_THAT(flag.Parse({"-myflag=abc"}), IsError());
  ASSERT_THAT(flag.Parse({"--myflag=def"}), IsError());
  ASSERT_THAT(flag.Parse({"-myflag", "abc"}), IsError());
  ASSERT_THAT(flag.Parse({"--myflag", "def"}), IsError());
}

TEST(FlagParser, InvalidFlagGuard) {
  auto flag = InvalidFlagGuard();
  ASSERT_THAT(flag.Parse({}), IsOk());
  ASSERT_THAT(flag.Parse({"positional"}), IsOk());
  ASSERT_THAT(flag.Parse({"positional", "positional2"}), IsOk());
  ASSERT_THAT(flag.Parse({"-flag"}), IsError());
  ASSERT_THAT(flag.Parse({"-"}), IsError());
}

TEST(FlagParser, UnexpectedArgumentGuard) {
  auto flag = UnexpectedArgumentGuard();
  ASSERT_THAT(flag.Parse({}), IsOk());
  ASSERT_THAT(flag.Parse({"positional"}), IsError());
  ASSERT_THAT(flag.Parse({"positional", "positional2"}), IsError());
  ASSERT_THAT(flag.Parse({"-flag"}), IsError());
  ASSERT_THAT(flag.Parse({"-"}), IsError());
}

TEST(FlagParser, EndOfOptionMark) {
  std::vector<std::string> args{"-flag", "--", "-invalid_flag"};
  bool flag = false;
  std::vector<Flag> flags{GflagsCompatFlag("flag", flag), InvalidFlagGuard()};

  EXPECT_THAT(ConsumeFlags(flags, args), IsError());
  EXPECT_THAT(ConsumeFlags(flags, args,
                           /* recognize_end_of_option_mark */ true),
              IsOk());
  ASSERT_TRUE(flag);
}

TEST(FlagParser, ConsumesConstrainedEquals) {
  std::vector<std::string> args{"--name=abc", "status", "--name=def"};

  std::string name;
  Flag name_flag = GflagsCompatFlag("name", name);

  std::vector<Flag> flags = {name_flag};
  EXPECT_THAT(ConsumeFlagsConstrained(flags, args), IsOk());

  std::vector<std::string> expected_args = {"status", "--name=def"};
  EXPECT_EQ(args, expected_args);
  EXPECT_EQ(name, "abc");
}

TEST(FlagParser, ConsumesConstrainedSeparated) {
  std::vector<std::string> args{"--name", "abc", "status", "--name", "def"};

  std::string name;
  Flag name_flag = GflagsCompatFlag("name", name);

  std::vector<Flag> flags = {name_flag};
  EXPECT_THAT(ConsumeFlagsConstrained(flags, args), IsOk());

  std::vector<std::string> expected_args = {"status", "--name", "def"};
  EXPECT_EQ(args, expected_args);
  EXPECT_EQ(name, "abc");
}

TEST(FlagParser, OptionalStringFlag_DefaultOptNotPresent) {
  std::optional<std::string> value;

  // Default options: None
  auto flag_default = GflagsCompatFlag("myflag", value);

  // Not present, should remain default (nullopt)
  value = std::nullopt;
  ASSERT_THAT(flag_default.Parse({}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalStringFlag_DefaultOptPresent) {
  std::optional<std::string> value;

  // Default options: None
  auto flag_default = GflagsCompatFlag("myflag", value);

  // Present with value
  value = std::nullopt;
  ASSERT_THAT(flag_default.Parse({"--myflag=foo"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, "foo");
}

TEST(FlagParser, OptionalStringFlag_DefaultOptEmtpyFlag) {
  std::optional<std::string> value;

  // Default options: None
  auto flag_default = GflagsCompatFlag("myflag", value);

  // Present with empty value
  value = std::nullopt;
  ASSERT_THAT(flag_default.Parse({"--myflag="}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, "");
}

TEST(FlagParser, OptionalStringFlag_EmptyOptEmtpyFlag) {
  std::optional<std::string> value;

  // With EmptyString option
  auto flag_empty_string = GflagsCompatFlag("myflag", value, CoerceToNullopt::EmptyString);

  // Present with empty value -> should be nullopt
  value = "before";
  ASSERT_THAT(flag_empty_string.Parse({"--myflag="}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalStringFlag_EmptyOptPresent) {
  std::optional<std::string> value;

  // With EmptyString option
  auto flag_empty_string = GflagsCompatFlag("myflag", value, CoerceToNullopt::EmptyString);

  // Present with value -> should be value
  value = std::nullopt;
  ASSERT_THAT(flag_empty_string.Parse({"--myflag=bar"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, "bar");
}

TEST(FlagParser, OptionalStringFlag_UnsetOptUnsetValue) {
  std::optional<std::string> value;

  // With UnsetKeyword option
  auto flag_unset = GflagsCompatFlag("myflag", value, CoerceToNullopt::UnsetKeyword);

  // Present with "unset" -> should be nullopt
  value = "before";
  ASSERT_THAT(flag_unset.Parse({"--myflag=unset"}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalStringFlag_UnsetOptOtherValue) {
  std::optional<std::string> value;

  // With UnsetKeyword option
  auto flag_unset = GflagsCompatFlag("myflag", value, CoerceToNullopt::UnsetKeyword);

  // Present with other value -> should be value
  value = std::nullopt;
  ASSERT_THAT(flag_unset.Parse({"--myflag=baz"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, "baz");
}

TEST(FlagParser, OptionalStringVectorFlag_DefaultOptNotPresent) {
  std::optional<std::vector<std::string>> value;

  // Default options: None
  auto flag_default = GflagsCompatFlag("myflag", value);

  // Not present, should remain default (nullopt)
  value = std::nullopt;
  ASSERT_THAT(flag_default.Parse({}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalStringVectorFlag_DefaultOptPresent) {
  std::optional<std::vector<std::string>> value;

  // Default options: None
  auto flag_default = GflagsCompatFlag("myflag", value);

  // Present with value
  value = std::nullopt;
  ASSERT_THAT(flag_default.Parse({"--myflag=foo,bar"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, std::vector<std::string>({"foo", "bar"}));
}

TEST(FlagParser, OptionalStringVectorFlag_DefaultOptEmptyValue) {
  std::optional<std::vector<std::string>> value;

  // Default options: None
  auto flag_default = GflagsCompatFlag("myflag", value);

  // Present with empty value (should be empty vector, not nullopt, because opt is None)
  value = std::nullopt;
  ASSERT_THAT(flag_default.Parse({"--myflag="}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_TRUE(value->empty());
}

TEST(FlagParser, OptionalStringVectorFlag_EmptyOptEmptyValue) {
  std::optional<std::vector<std::string>> value;

  // With EmptyString option
  auto flag_empty_string = GflagsCompatFlag("myflag", value, CoerceToNullopt::EmptyString);

  // Present with empty value -> should be nullopt
  value = std::vector<std::string>{"before"};
  ASSERT_THAT(flag_empty_string.Parse({"--myflag="}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalStringVectorFlag_EmptyOptPresent) {
  std::optional<std::vector<std::string>> value;

  // With EmptyString option
  auto flag_empty_string = GflagsCompatFlag("myflag", value, CoerceToNullopt::EmptyString);

  // Present with value -> should be value
  value = std::nullopt;
  ASSERT_THAT(flag_empty_string.Parse({"--myflag=bar,baz"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, std::vector<std::string>({"bar", "baz"}));
}

TEST(FlagParser, OptionalStringVectorFlag_UnsetOptUnsetValue) {
  std::optional<std::vector<std::string>> value;

  // With UnsetKeyword option
  auto flag_unset = GflagsCompatFlag("myflag", value, CoerceToNullopt::UnsetKeyword);

  // Present with "unset" -> should be nullopt
  value = std::vector<std::string>{"before"};
  ASSERT_THAT(flag_unset.Parse({"--myflag=unset"}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalStringVectorFlag_UnsetOptPresent) {
  std::optional<std::vector<std::string>> value;

  // With UnsetKeyword option
  auto flag_unset = GflagsCompatFlag("myflag", value, CoerceToNullopt::UnsetKeyword);

  // Present with other value -> should be value
  value = std::nullopt;
  ASSERT_THAT(flag_unset.Parse({"--myflag=baz"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, std::vector<std::string>({"baz"}));
}

TEST(FlagParser, OptionalBoolFlag_DefaultOptNotPresent) {
  std::optional<bool> value;
  auto flag = GflagsCompatFlag("myflag", value);
  value = std::nullopt;
  ASSERT_THAT(flag.Parse({}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalBoolFlag_DefaultOptPresent_FlagOnly) {
  std::optional<bool> value;
  auto flag = GflagsCompatFlag("myflag", value);
  value = std::nullopt;
  ASSERT_THAT(flag.Parse({"--myflag"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_TRUE(*value);
}

TEST(FlagParser, OptionalBoolFlag_DefaultOptPresent_TrueValue) {
  std::optional<bool> value;
  auto flag = GflagsCompatFlag("myflag", value);
  value = std::nullopt;
  ASSERT_THAT(flag.Parse({"--myflag=true"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_TRUE(*value);
}

TEST(FlagParser, OptionalBoolFlag_DefaultOptPresent_FalseValue) {
  std::optional<bool> value;
  auto flag = GflagsCompatFlag("myflag", value);
  value = std::nullopt;
  ASSERT_THAT(flag.Parse({"--myflag=false"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_FALSE(*value);
}

TEST(FlagParser, OptionalBoolFlag_NoFlag) {
  std::optional<bool> value;
  auto flag = GflagsCompatFlag("myflag", value);
  value = std::nullopt;
  ASSERT_THAT(flag.Parse({"--nomyflag"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_FALSE(*value);
}

TEST(FlagParser, OptionalBoolFlag_EmptyOptEmptyFlag) {
  std::optional<bool> value;
  auto flag = GflagsCompatFlag("myflag", value, CoerceToNullopt::EmptyString);
  value = true;
  ASSERT_THAT(flag.Parse({"--myflag="}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalBoolFlag_UnsetOptUnsetValue) {
  std::optional<bool> value;
  auto flag = GflagsCompatFlag("myflag", value, CoerceToNullopt::UnsetKeyword);
  value = true;
  ASSERT_THAT(flag.Parse({"--myflag=unset"}), IsOk());
  ASSERT_FALSE(value.has_value());
}

TEST(FlagParser, OptionalInt64Flag_Present) {
  std::optional<int64_t> value;
  auto flag = GflagsCompatFlag("myflag", value);
  value = std::nullopt;
  ASSERT_THAT(flag.Parse({"--myflag=123456789012345"}), IsOk());
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(*value, 123456789012345LL);
}

TEST(FlagParser, OptionalInt64Flag_NotPresent) {
  std::optional<int64_t> value;
  auto flag = GflagsCompatFlag("myflag", value);
  value = std::nullopt;
  ASSERT_THAT(flag.Parse({}), IsOk());
  ASSERT_FALSE(value.has_value());
}

}  // namespace cuttlefish
