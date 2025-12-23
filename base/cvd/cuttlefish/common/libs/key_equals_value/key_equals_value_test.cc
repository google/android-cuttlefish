//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"

#include <string>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/result_matchers.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

TEST(KeyEqualsValue, Deserialize) {
  std::string serialized = "key1 = value1 \n key2 = value2";

  std::map<std::string, std::string> expected = {{"key1", "value1"},
                                                 {"key2", "value2"}};
  ASSERT_THAT(ParseKeyEqualsValue(serialized), IsOkAndValue(expected));
}

TEST(KeyEqualsValue, Serialize) {
  std::map<std::string, std::string> misc_info = {{"key1", "value1"},
                                                  {"key2", "value2"}};

  EXPECT_EQ(SerializeKeyEqualsValue(misc_info), "key1=value1\nkey2=value2\n");
}

TEST(KeyEqualsValue, SerializeDeserialize) {
  std::map<std::string, std::string> misc_info = {{"key1", "value1"},
                                                  {"key2", "value2"}};

  std::string serialized = SerializeKeyEqualsValue(misc_info);

  ASSERT_THAT(ParseKeyEqualsValue(serialized), IsOkAndValue(misc_info));
}

TEST(KeyEqualsValue, DeserializeDuplicateKeySameValue) {
  std::string serialized = "key1=value1\nkey1=value1";

  std::map<std::string, std::string> expected = {{"key1", "value1"}};
  ASSERT_THAT(ParseKeyEqualsValue(serialized), IsOkAndValue(expected));
}

TEST(KeyEqualsValue, DeserializeDuplicateKeyDifferentValue) {
  std::string serialized = "key1=value1\nkey1=value2";

  ASSERT_THAT(ParseKeyEqualsValue(serialized), IsError());
}

TEST(KeyEqualsValue, EmptyLines) {
  std::string serialized = "\n\n\n\n\n\n";

  ASSERT_THAT(ParseKeyEqualsValue(serialized),
              IsOkAndValue(std::map<std::string, std::string>()));
}

}  // namespace
}  // namespace cuttlefish
