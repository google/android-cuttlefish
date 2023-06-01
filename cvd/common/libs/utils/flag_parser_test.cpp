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

#include <common/libs/utils/flag_parser.h>

#include <gtest/gtest.h>
#include <libxml/tree.h>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace cuttlefish {

TEST(FlagParser, DuplicateAlias) {
  FlagAlias alias = {FlagAliasMode::kFlagExact, "--flag"};
  ASSERT_DEATH({ Flag().Alias(alias).Alias(alias); }, "Duplicate flag alias");
}

TEST(FlagParser, ConflictingAlias) {
  FlagAlias exact_alias = {FlagAliasMode::kFlagExact, "--flag"};
  FlagAlias following_alias = {FlagAliasMode::kFlagConsumesFollowing, "--flag"};
  ASSERT_DEATH({ Flag().Alias(exact_alias).Alias(following_alias); },
               "Overlapping flag aliases");
}

TEST(FlagParser, StringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_TRUE(flag.Parse({"-myflag=a"}));
  ASSERT_EQ(value, "a");
  ASSERT_TRUE(flag.Parse({"--myflag=b"}));
  ASSERT_EQ(value, "b");
  ASSERT_TRUE(flag.Parse({"-myflag", "c"}));
  ASSERT_EQ(value, "c");
  ASSERT_TRUE(flag.Parse({"--myflag", "d"}));
  ASSERT_EQ(value, "d");
  ASSERT_TRUE(flag.Parse({"--myflag="}));
  ASSERT_EQ(value, "");
}

std::optional<std::map<std::string, std::string>> flagXml(const Flag& f) {
  std::stringstream xml_stream;
  if (!f.WriteGflagsCompatXml(xml_stream)) {
    return {};
  }
  auto xml = xml_stream.str();
  // Holds all memory for the parsed structure.
  std::unique_ptr<xmlDoc, xmlFreeFunc> doc(
      xmlReadMemory(xml.c_str(), xml.size(), nullptr, nullptr, 0), xmlFree);
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
  auto flag = Flag().Alias({FlagAliasMode::kFlagExact, "--flag"});
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
  ASSERT_TRUE(flag.Parse({"-myflag=a", "--myflag", "b"}));
  ASSERT_EQ(value, "b");
}

TEST(FlagParser, RepeatedListFlag) {
  std::vector<std::string> elems;
  auto flag = GflagsCompatFlag("myflag");
  flag.Setter([&elems](const FlagMatch& match) {
    elems.push_back(match.value);
    return true;
  });
  ASSERT_TRUE(flag.Parse({"-myflag=a", "--myflag", "b"}));
  ASSERT_EQ(elems, (std::vector<std::string>{"a", "b"}));
}

TEST(FlagParser, FlagRemoval) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  std::vector<std::string> flags = {"-myflag=a", "-otherflag=c"};
  ASSERT_TRUE(flag.Parse(flags));
  ASSERT_EQ(value, "a");
  ASSERT_EQ(flags, std::vector<std::string>{"-otherflag=c"});
  flags = {"-otherflag=a", "-myflag=c"};
  ASSERT_TRUE(flag.Parse(flags));
  ASSERT_EQ(value, "c");
  ASSERT_EQ(flags, std::vector<std::string>{"-otherflag=a"});
}

TEST(FlagParser, IntFlag) {
  std::int32_t value = 0;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_TRUE(flag.Parse({"-myflag=5"}));
  ASSERT_EQ(value, 5);
  ASSERT_TRUE(flag.Parse({"--myflag=6"}));
  ASSERT_EQ(value, 6);
  ASSERT_TRUE(flag.Parse({"-myflag", "7"}));
  ASSERT_EQ(value, 7);
  ASSERT_TRUE(flag.Parse({"--myflag", "8"}));
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
  ASSERT_TRUE(flag.Parse({"-myflag"}));
  ASSERT_TRUE(value);

  value = false;
  ASSERT_TRUE(flag.Parse({"--myflag"}));
  ASSERT_TRUE(value);

  value = false;
  ASSERT_TRUE(flag.Parse({"-myflag=true"}));
  ASSERT_TRUE(value);

  value = false;
  ASSERT_TRUE(flag.Parse({"--myflag=true"}));
  ASSERT_TRUE(value);

  value = true;
  ASSERT_TRUE(flag.Parse({"-nomyflag"}));
  ASSERT_FALSE(value);

  value = true;
  ASSERT_TRUE(flag.Parse({"--nomyflag"}));
  ASSERT_FALSE(value);

  value = true;
  ASSERT_TRUE(flag.Parse({"-myflag=false"}));
  ASSERT_FALSE(value);

  value = true;
  ASSERT_TRUE(flag.Parse({"--myflag=false"}));
  ASSERT_FALSE(value);

  ASSERT_FALSE(flag.Parse({"--myflag=nonsense"}));
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
  std::int32_t int_value = 0;
  std::string string_value;
  auto int_flag = GflagsCompatFlag("int", int_value);
  auto string_flag = GflagsCompatFlag("string", string_value);
  std::vector<Flag> flags = {int_flag, string_flag};
  ASSERT_TRUE(ParseFlags(flags, {"-int=5", "-string=a"}));
  ASSERT_EQ(int_value, 5);
  ASSERT_EQ(string_value, "a");
  ASSERT_TRUE(ParseFlags(flags, {"--int=6", "--string=b"}));
  ASSERT_EQ(int_value, 6);
  ASSERT_EQ(string_value, "b");
  ASSERT_TRUE(ParseFlags(flags, {"-int", "7", "-string", "c"}));
  ASSERT_EQ(int_value, 7);
  ASSERT_EQ(string_value, "c");
  ASSERT_TRUE(ParseFlags(flags, {"--int", "8", "--string", "d"}));
  ASSERT_EQ(int_value, 8);
  ASSERT_EQ(string_value, "d");
}

TEST(FlagParser, StringVectorFlag) {
  std::vector<std::string> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_FALSE(flag.Parse({"--myflag="}));
  ASSERT_TRUE(value.empty());

  ASSERT_TRUE(flag.Parse({"--myflag=foo"}));
  ASSERT_EQ(value, std::vector<std::string>({"foo"}));

  ASSERT_TRUE(flag.Parse({"--myflag=foo,bar"}));
  ASSERT_EQ(value, std::vector<std::string>({"foo", "bar"}));

  ASSERT_TRUE(flag.Parse({"--myflag=,bar"}));
  ASSERT_EQ(value, std::vector<std::string>({"", "bar"}));

  ASSERT_TRUE(flag.Parse({"--myflag=foo,"}));
  ASSERT_EQ(value, std::vector<std::string>({"foo", ""}));

  ASSERT_TRUE(flag.Parse({"--myflag=,"}));
  ASSERT_EQ(value, std::vector<std::string>({"", ""}));
}

TEST(FlagParser, BoolVectorFlag) {
  std::vector<bool> value;
  bool default_value = true;
  auto flag = GflagsCompatFlag("myflag", value, default_value);

  ASSERT_FALSE(flag.Parse({"--myflag="}));
  ASSERT_TRUE(value.empty());

  ASSERT_FALSE(flag.Parse({"--myflag=foo"}));
  ASSERT_TRUE(value.empty());

  ASSERT_FALSE(flag.Parse({"--myflag=true,bar"}));
  ASSERT_TRUE(value.empty());

  ASSERT_TRUE(flag.Parse({"--myflag=true"}));
  ASSERT_EQ(value, std::vector<bool>({true}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true");

  ASSERT_TRUE(flag.Parse({"--myflag=true,false"}));
  ASSERT_EQ(value, std::vector<bool>({true, false}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true,false");

  ASSERT_TRUE(flag.Parse({"--myflag=,false"}));
  ASSERT_EQ(value, std::vector<bool>({true, false}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true,false");

  ASSERT_TRUE(flag.Parse({"--myflag=true,"}));
  ASSERT_EQ(value, std::vector<bool>({true, true}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true,true");

  ASSERT_TRUE(flag.Parse({"--myflag=,"}));
  ASSERT_EQ(value, std::vector<bool>({true, true}));
  ASSERT_TRUE(flagXml(flag));
  ASSERT_EQ((*flagXml(flag))["default"], "true,true");
}

TEST(FlagParser, InvalidStringFlag) {
  std::string value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_FALSE(flag.Parse({"-myflag"}));
  ASSERT_FALSE(flag.Parse({"--myflag"}));
}

TEST(FlagParser, InvalidIntFlag) {
  int value;
  auto flag = GflagsCompatFlag("myflag", value);
  ASSERT_FALSE(flag.Parse({"-myflag"}));
  ASSERT_FALSE(flag.Parse({"--myflag"}));
  ASSERT_FALSE(flag.Parse({"-myflag=abc"}));
  ASSERT_FALSE(flag.Parse({"--myflag=def"}));
  ASSERT_FALSE(flag.Parse({"-myflag", "abc"}));
  ASSERT_FALSE(flag.Parse({"--myflag", "def"}));
}

TEST(FlagParser, InvalidFlagGuard) {
  auto flag = InvalidFlagGuard();
  ASSERT_TRUE(flag.Parse({}));
  ASSERT_TRUE(flag.Parse({"positional"}));
  ASSERT_TRUE(flag.Parse({"positional", "positional2"}));
  ASSERT_FALSE(flag.Parse({"-flag"}));
  ASSERT_FALSE(flag.Parse({"-"}));
}

TEST(FlagParser, UnexpectedArgumentGuard) {
  auto flag = UnexpectedArgumentGuard();
  ASSERT_TRUE(flag.Parse({}));
  ASSERT_FALSE(flag.Parse({"positional"}));
  ASSERT_FALSE(flag.Parse({"positional", "positional2"}));
  ASSERT_FALSE(flag.Parse({"-flag"}));
  ASSERT_FALSE(flag.Parse({"-"}));
}

class FlagConsumesArbitraryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    elems_.clear();
    flag_ = Flag()
                .Alias({FlagAliasMode::kFlagConsumesArbitrary, "--flag"})
                .Setter([this](const FlagMatch& match) {
                  elems_.push_back(match.value);
                  return true;
                });
  }
  Flag flag_;
  std::vector<std::string> elems_;
};

TEST_F(FlagConsumesArbitraryTest, NoValues) {
  std::vector<std::string> inputs = {"--flag"};
  ASSERT_TRUE(flag_.Parse(inputs));
  ASSERT_EQ(inputs, (std::vector<std::string>{}));
  ASSERT_EQ(elems_, (std::vector<std::string>{""}));
}

TEST_F(FlagConsumesArbitraryTest, OneValue) {
  std::vector<std::string> inputs = {"--flag", "value"};
  ASSERT_TRUE(flag_.Parse(inputs));
  ASSERT_EQ(inputs, (std::vector<std::string>{}));
  ASSERT_EQ(elems_, (std::vector<std::string>{"value", ""}));
}

TEST_F(FlagConsumesArbitraryTest, TwoValues) {
  std::vector<std::string> inputs = {"--flag", "value1", "value2"};
  ASSERT_TRUE(flag_.Parse(inputs));
  ASSERT_EQ(inputs, (std::vector<std::string>{}));
  ASSERT_EQ(elems_, (std::vector<std::string>{"value1", "value2", ""}));
}

TEST_F(FlagConsumesArbitraryTest, NoValuesOtherFlag) {
  std::vector<std::string> inputs = {"--flag", "--otherflag"};
  ASSERT_TRUE(flag_.Parse(inputs));
  ASSERT_EQ(inputs, (std::vector<std::string>{"--otherflag"}));
  ASSERT_EQ(elems_, (std::vector<std::string>{""}));
}

TEST_F(FlagConsumesArbitraryTest, OneValueOtherFlag) {
  std::vector<std::string> inputs = {"--flag", "value", "--otherflag"};
  ASSERT_TRUE(flag_.Parse(inputs));
  ASSERT_EQ(inputs, (std::vector<std::string>{"--otherflag"}));
  ASSERT_EQ(elems_, (std::vector<std::string>{"value", ""}));
}

TEST_F(FlagConsumesArbitraryTest, TwoValuesOtherFlag) {
  std::vector<std::string> inputs = {"--flag", "v1", "v2", "--otherflag"};
  ASSERT_TRUE(flag_.Parse(inputs));
  ASSERT_EQ(inputs, (std::vector<std::string>{"--otherflag"}));
  ASSERT_EQ(elems_, (std::vector<std::string>{"v1", "v2", ""}));
}

}  // namespace cuttlefish
