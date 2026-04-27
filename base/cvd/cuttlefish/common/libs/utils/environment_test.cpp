/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/environment.h"

#include <cstdlib>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include "absl/cleanup/cleanup.h"

namespace cuttlefish {

TEST(EnvironmentTest, StringFromEnvString) {
  const std::string env_name = "TEST_ENV_VAR_STRING";
  const std::string env_value = "test_value";

  setenv(env_name.c_str(), env_value.c_str(), 1);
  absl::Cleanup cleanup = [&env_name]() { unsetenv(env_name.c_str()); };

  std::optional<std::string> result = StringFromEnv(env_name);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, env_value);
}

TEST(EnvironmentTest, StringFromEnvStringDefault) {
  const std::string env_name = "TEST_ENV_VAR_STRING_DEFAULT";
  const std::string env_value = "test_value";
  const std::string def_value = "default_value";

  setenv(env_name.c_str(), env_value.c_str(), 1);
  absl::Cleanup cleanup = [&env_name]() { unsetenv(env_name.c_str()); };

  EXPECT_EQ(StringFromEnv(env_name, def_value), env_value);
}

TEST(EnvironmentTest, StringFromEnvStringDefaultFallback) {
  const std::string env_name = "TEST_ENV_VAR_STRING_DEFAULT_FALLBACK";
  const std::string def_value = "default_value";

  EXPECT_EQ(StringFromEnv(env_name, def_value), def_value);
}

TEST(EnvironmentTest, StringFromEnvCharPtr) {
  const char* env_name = "TEST_ENV_VAR_CHAR_PTR";
  const char* env_value = "test_value";

  setenv(env_name, env_value, 1);
  absl::Cleanup cleanup = [env_name]() { unsetenv(env_name); };

  std::optional<std::string> result = StringFromEnv(env_name);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, env_value);
}

TEST(EnvironmentTest, StringFromEnvCharPtrDefault) {
  const char* env_name = "TEST_ENV_VAR_CHAR_PTR_DEFAULT";
  const char* env_value = "test_value";
  const std::string def_value = "default_value";

  setenv(env_name, env_value, 1);
  absl::Cleanup cleanup = [env_name]() { unsetenv(env_name); };

  EXPECT_EQ(StringFromEnv(env_name, def_value), env_value);
}

TEST(EnvironmentTest, StringFromEnvCharPtrDefaultFallback) {
  const char* env_name = "TEST_ENV_VAR_CHAR_PTR_DEFAULT_FALLBACK";
  const std::string def_value = "default_value";

  EXPECT_EQ(StringFromEnv(env_name, def_value), def_value);
}

TEST(EnvironmentTest, StringFromEnvStringView) {
  const std::string env_name_str = "TEST_ENV_VAR_STRING_VIEW";
  const std::string env_value = "test_value";
  std::string_view env_name = env_name_str;

  setenv(env_name_str.c_str(), env_value.c_str(), 1);
  absl::Cleanup cleanup = [&env_name_str]() { unsetenv(env_name_str.c_str()); };

  std::optional<std::string> result = StringFromEnv(env_name);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, env_value);
}

TEST(EnvironmentTest, StringFromEnvStringViewDefault) {
  const std::string env_name_str = "TEST_ENV_VAR_STRING_VIEW_DEFAULT";
  const std::string env_value = "test_value";
  const std::string def_value = "default_value";
  std::string_view env_name = env_name_str;

  setenv(env_name_str.c_str(), env_value.c_str(), 1);
  absl::Cleanup cleanup = [&env_name_str]() { unsetenv(env_name_str.c_str()); };

  EXPECT_EQ(StringFromEnv(env_name, def_value), env_value);
}

TEST(EnvironmentTest, StringFromEnvStringViewDefaultFallback) {
  const std::string env_name_str = "TEST_ENV_VAR_STRING_VIEW_DEFAULT_FALLBACK";
  const std::string def_value = "default_value";
  std::string_view env_name = env_name_str;

  EXPECT_EQ(StringFromEnv(env_name, def_value), def_value);
}

}  // namespace cuttlefish
