/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "properties.h"

namespace avd {
namespace {

TEST(PropertiesTest, CanParseEmptyLine) {
  char line1[] = "\0";
  char line2[] = "    ";
  char line3[] = "\t\n";
  char* key = line1;
  char* value = line1;

  EXPECT_TRUE(PropertyLineToKeyValuePair(line1, &key, &value));
  EXPECT_EQ(NULL, key);
  EXPECT_EQ(NULL, value);

  key = line1;
  value = line1;
  EXPECT_TRUE(PropertyLineToKeyValuePair(line2, &key, &value));
  EXPECT_EQ(NULL, key);
  EXPECT_EQ(NULL, value);

  key = line1;
  value = line1;
  EXPECT_TRUE(PropertyLineToKeyValuePair(line3, &key, &value));
  EXPECT_EQ(NULL, key);
  EXPECT_EQ(NULL, value);
}

TEST(PropertiesTest, CanParseComment) {
  char line1[] = "# abcdefg";
  char line2[] = "     # abcdefg";
  char* key = line1;
  char* value = line1;

  EXPECT_TRUE(PropertyLineToKeyValuePair(line1, &key, &value));
  EXPECT_EQ(NULL, key);
  EXPECT_EQ(NULL, value);

  key = line1;
  value = line1;
  EXPECT_TRUE(PropertyLineToKeyValuePair(line2, &key, &value));
  EXPECT_EQ(NULL, key);
  EXPECT_EQ(NULL, value);
}

TEST(PropertiesTest, CanParseValidAttributeLine) {
  char line1[] = "abc=defgh";
  char line2[] = "ijk=lmnop\n";
  char line3[] = "      qrs=tuv        \n";
  char line4[] = "ijk=\n";
  char* key = line1;
  char* value = line1;

  EXPECT_TRUE(PropertyLineToKeyValuePair(line1, &key, &value));
  EXPECT_STREQ("abc", key);
  EXPECT_STREQ("defgh", value);

  EXPECT_TRUE(PropertyLineToKeyValuePair(line2, &key, &value));
  EXPECT_STREQ("ijk", key);
  EXPECT_STREQ("lmnop", value);

  EXPECT_TRUE(PropertyLineToKeyValuePair(line3, &key, &value));
  EXPECT_STREQ("qrs", key);
  EXPECT_STREQ("tuv", value);

  key = line1;
  value = line1;
  EXPECT_TRUE(PropertyLineToKeyValuePair(line4, &key, &value));
  EXPECT_STREQ("ijk", key);
  EXPECT_STREQ("", value);
}

TEST(PropertiesTest, FailsAtInvalidArgument) {
  char line1[] = "abc";
  char line2[] = "=lmn\n";
  char* key = line1;
  char* value = line1;

  EXPECT_FALSE(PropertyLineToKeyValuePair(line1, &key, &value));
  EXPECT_EQ(NULL, key);
  EXPECT_EQ(NULL, value);

  key = line1;
  value = line1;
  EXPECT_FALSE(PropertyLineToKeyValuePair(line2, &key, &value));
  EXPECT_EQ(NULL, key);
  EXPECT_EQ(NULL, value);

}

// These are the cases I am uncertain about.
// While these will not cause any out-of-bounds access or other direct issues,
// the keys or values may not exactly be what you'd expect.
TEST(PropertiesTest, InterestingOddCases) {
  char line1[] = "abc==";
  char line2[] = "ijk= # abcde";
  char line3[] = "lmn = oper";
  char* key = line1;
  char* value = line1;

  EXPECT_TRUE(PropertyLineToKeyValuePair(line1, &key, &value));
  EXPECT_STREQ("abc", key);
  EXPECT_STREQ("=", value);

  EXPECT_TRUE(PropertyLineToKeyValuePair(line2, &key, &value));
  EXPECT_STREQ("ijk", key);
  EXPECT_STREQ(" # abcde", value);

  EXPECT_TRUE(PropertyLineToKeyValuePair(line3, &key, &value));
  EXPECT_STREQ("lmn ", key);
  EXPECT_STREQ(" oper", value);
}

}  // namespace
}  // namespace avd
