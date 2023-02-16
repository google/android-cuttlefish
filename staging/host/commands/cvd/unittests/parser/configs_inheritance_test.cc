/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <algorithm>
#include <fstream>
#include <iostream>

#include <android-base/file.h>

#include <gtest/gtest.h>

#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

namespace cuttlefish {

TEST(FlagsInheritanceTest, MergeTwoIndependentJson) {
  const char* dst_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "memory_mb": 2048
            }
        }
    ]
}
  )"""";

  const char* src_string = R""""(
{
    "instances" :
    [
        {
            "graphics":{
                "displays":[
                    {
                        "width": 720,
                        "height": 1280,
                        "dpi": 320
                    }
                ]
			}
        }
    ]
}
  )"""";

  Json::Value src_object, dst_object;
  std::string src_text(src_string);
  std::string dst_text(dst_string);
  EXPECT_TRUE(ParseJsonString(dst_text, dst_object)) << "Invalid Json string";
  EXPECT_TRUE(ParseJsonString(src_text, src_object)) << "Invalid Json string";

  cuttlefish::MergeTwoJsonObjs(dst_object, src_object);
  EXPECT_TRUE(dst_object["instances"][0].isMember("graphics"));
  EXPECT_TRUE(dst_object["instances"][0]["graphics"].isMember("displays"));
  EXPECT_TRUE(
      dst_object["instances"][0]["graphics"]["displays"][0].isMember("width"));
  EXPECT_TRUE(
      dst_object["instances"][0]["graphics"]["displays"][0].isMember("height"));
  EXPECT_TRUE(
      dst_object["instances"][0]["graphics"]["displays"][0].isMember("dpi"));

  EXPECT_EQ(dst_object["instances"][0]["graphics"]["displays"][0]["width"],
            720);
  EXPECT_EQ(dst_object["instances"][0]["graphics"]["displays"][0]["height"],
            1280);
  EXPECT_EQ(dst_object["instances"][0]["graphics"]["displays"][0]["dpi"], 320);
}

TEST(FlagsInheritanceTest, MergeTwoOverlappedJson) {
  const char* dst_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "memory_mb": 1024
            }
        }
    ]
}
  )"""";

  const char* src_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "memory_mb": 2048
            },
            "graphics":{
                "displays":[
                    {
                        "width": 720,
                        "height": 1280,
                        "dpi": 320
                    }
                ]
			}
        }
    ]
}
  )"""";

  Json::Value src_object, dst_object;
  std::string src_text(src_string);
  std::string dst_text(dst_string);
  EXPECT_TRUE(ParseJsonString(dst_text, dst_object)) << "Invalid Json string";
  EXPECT_TRUE(ParseJsonString(src_text, src_object)) << "Invalid Json string";

  cuttlefish::MergeTwoJsonObjs(dst_object, src_object);
  EXPECT_TRUE(dst_object["instances"][0].isMember("graphics"));
  EXPECT_TRUE(dst_object["instances"][0]["graphics"].isMember("displays"));
  EXPECT_TRUE(
      dst_object["instances"][0]["graphics"]["displays"][0].isMember("width"));
  EXPECT_TRUE(
      dst_object["instances"][0]["graphics"]["displays"][0].isMember("height"));
  EXPECT_TRUE(
      dst_object["instances"][0]["graphics"]["displays"][0].isMember("dpi"));

  EXPECT_EQ(dst_object["instances"][0]["graphics"]["displays"][0]["width"],
            720);
  EXPECT_EQ(dst_object["instances"][0]["graphics"]["displays"][0]["height"],
            1280);
  EXPECT_EQ(dst_object["instances"][0]["graphics"]["displays"][0]["dpi"], 320);
  // Check for overlapped values
  EXPECT_EQ(dst_object["instances"][0]["vm"]["memory_mb"], 2048);
}

}  // namespace cuttlefish
