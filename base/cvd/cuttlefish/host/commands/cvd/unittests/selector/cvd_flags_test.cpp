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

#include "host/commands/cvd/unittests/selector/cvd_flags_helper.h"

namespace cuttlefish {

TEST_F(CvdFlagCollectionTest, Init) {
  auto output_result = flag_collection_.FilterFlags(input_);
  ASSERT_TRUE(output_result.ok()) << output_result.error().Trace();
}

TEST_F(CvdFlagCollectionTest, Leftover) {
  auto output_result = flag_collection_.FilterFlags(input_);
  ASSERT_TRUE(output_result.ok()) << output_result.error().Trace();
  ASSERT_EQ(input_, cvd_common::Args{"--not_consumed"});
}

TEST_F(CvdFlagCollectionTest, AllGivenFlagsListed) {
  auto output_result = flag_collection_.FilterFlags(input_);
  ASSERT_TRUE(output_result.ok()) << output_result.error().Trace();
  ASSERT_EQ(input_, cvd_common::Args{"--not_consumed"});
  auto output = std::move(*output_result);

  ASSERT_FALSE(Contains(output, "help"));
  ASSERT_TRUE(Contains(output, "name"));
  ASSERT_TRUE(Contains(output, "enable_vnc"));
  ASSERT_TRUE(Contains(output, "id"));
  ASSERT_FALSE(Contains(output, "not-given"));
  ASSERT_FALSE(Contains(output, "not-consumed"));
}

TEST(CvdFlagTest, FlagProxyFilter) {
  CvdFlag<std::string> no_default("no_default");
  cvd_common::Args has_flag_args{"--no_default=Hello"};
  cvd_common::Args not_has_flag_args{"--bar --foo=name --enable_vnc"};
  cvd_common::Args empty_args{""};

  CvdFlagProxy no_default_proxy(std::move(no_default));
  auto expected_hello_opt_result = no_default_proxy.FilterFlag(has_flag_args);
  auto expected_null_result = no_default_proxy.FilterFlag(not_has_flag_args);
  auto expected_null_result2 = no_default_proxy.FilterFlag(empty_args);

  ASSERT_TRUE(expected_hello_opt_result.ok());
  ASSERT_TRUE(expected_null_result.ok());
  ASSERT_TRUE(expected_null_result2.ok());

  ASSERT_TRUE(*expected_hello_opt_result);
  auto value_result = Get<std::string>(**expected_hello_opt_result);
  ASSERT_TRUE(value_result.ok());
  ASSERT_EQ(*value_result, "Hello");
  ASSERT_FALSE(*expected_null_result);
  ASSERT_FALSE(*expected_null_result2);

  ASSERT_TRUE(has_flag_args.empty());
  ASSERT_EQ(not_has_flag_args,
            cvd_common::Args{"--bar --foo=name --enable_vnc"});
}

}  // namespace cuttlefish
