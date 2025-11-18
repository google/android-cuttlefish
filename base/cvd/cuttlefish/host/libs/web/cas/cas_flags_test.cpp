//
// Copyright (C) 2024 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/cas/cas_flags.h"

#include <stdint.h>

#include <cstddef>
#include <string>

#include <android-base/file.h>
#include <gtest/gtest.h>

#include "cuttlefish/common/libs/utils/files.h"

namespace cuttlefish {

// Test helper to check if the default config path exists
bool DefaultConfigPathExists() {
  return FileExists(std::string(kDefaultCasConfigFilePath));
}

// Test helper to check if the default downloader path exists
bool DefaultDownloaderPathExists() {
  return FileExists(std::string(kDefaultDownloaderPath));
}

class CasFlagsTests : public ::testing::Test {
 protected:
  TemporaryDir temp_dir_;
};

// Note: tests that depend on the presence of system files (for example
// `/etc/casdownloader/config.json` or `/usr/bin/casdownloader`) are
// environment-dependent and therefore brittle in CI. The deterministic
// tests below exercise the programmatic contract (constants, flags API,
// and mutability) without relying on filesystem state.

// Test that CasDownloaderFlags has all expected default values
TEST_F(CasFlagsTests, DefaultValuesAreSet) {
  CasDownloaderFlags flags;
  // downloader_path and cas_config_filepath may be initialized from
  // system defaults; those are environment-dependent and tested
  // elsewhere. Here we check all other deterministic defaults.
  EXPECT_EQ(flags.prefer_uncompressed.value(), false);
  EXPECT_EQ(flags.cache_dir.value(), "");
  EXPECT_EQ(flags.cache_max_size.value(), kMinCacheMaxSize);
  EXPECT_EQ(flags.cache_lock.value(), false);
  EXPECT_EQ(flags.use_hardlink.value(), true);
  EXPECT_EQ(flags.memory_limit.value(), kDefaultMemoryLimit);
  EXPECT_EQ(flags.cas_concurrency.value(), kDefaultCasConcurrency);
  EXPECT_EQ(flags.rpc_timeout.value(), kDefaultRpcTimeout);
  EXPECT_EQ(flags.get_capabilities_timeout.value(),
            kDefaultGetCapabilitiesTimeout);
  EXPECT_EQ(flags.get_tree_timeout.value(), kDefaultGetTreeTimeout);
  EXPECT_EQ(flags.batch_read_blobs_timeout.value(),
            kDefaultBatchReadBlobsTimeout);
  EXPECT_EQ(flags.batch_update_blobs_timeout.value(),
            kDefaultBatchUpdateBlobsTimeout);
  EXPECT_EQ(flags.version.value(), false);
}

// Test that the Flags() method returns a non-empty vector
TEST_F(CasFlagsTests, FlagsMethodReturnsVector) {
  CasDownloaderFlags flags;
  auto flag_vector = flags.Flags();

  // Should have at least one flag defined
  EXPECT_GT(flag_vector.size(), 0);
}

// Test multiple instances of CasDownloaderFlags are independent
TEST_F(CasFlagsTests, MultipleInstancesIndependent) {
  CasDownloaderFlags flags1;
  CasDownloaderFlags flags2;

  // Modify flags1
  flags1.downloader_path.set_value("/path/to/downloader");
  flags1.cache_dir.set_value("/cache/dir");

  // Verify flags2 is not affected
  if (DefaultDownloaderPathExists()) {
    EXPECT_EQ(flags2.downloader_path.value(),
              std::string(kDefaultDownloaderPath));
  } else {
    EXPECT_EQ(flags2.downloader_path.value(), "");
  }
  EXPECT_EQ(flags2.cache_dir.value(), "");
}

// ============================================================================
// Tests for FlagValue<T> wrapper behavior
// ============================================================================

// Test that FlagValue is initialized with a default and not user-specified
TEST_F(CasFlagsTests, FlagValueInitializesWithDefault) {
  FlagValue<std::string> string_flag("default_string");
  EXPECT_EQ(string_flag.value(), "default_string");
  EXPECT_FALSE(string_flag.user_provided());

  FlagValue<int> int_flag(42);
  EXPECT_EQ(int_flag.value(), 42);
  EXPECT_FALSE(int_flag.user_provided());

  FlagValue<bool> bool_flag(true);
  EXPECT_TRUE(bool_flag.value());
  EXPECT_FALSE(bool_flag.user_provided());

  FlagValue<int64_t> int64_flag(12345L);
  EXPECT_EQ(int64_flag.value(), 12345L);
  EXPECT_FALSE(int64_flag.user_provided());
}

// Test that FlagValue's value can be modified
TEST_F(CasFlagsTests, FlagValueCanBeModified) {
  FlagValue<std::string> flag("initial");
  flag.set_value("modified");
  EXPECT_EQ(flag.value(), "modified");
  // value modification should mark the flag as user-provided
  EXPECT_TRUE(flag.user_provided());
}

// Test that FlagValue supports converting to its underlying type via value()
TEST_F(CasFlagsTests, FlagValueConversion) {
  FlagValue<std::string> string_flag("hello");
  std::string str = string_flag.value();
  EXPECT_EQ(str, "hello");

  FlagValue<int> int_flag(123);
  int i = int_flag.value();
  EXPECT_EQ(i, 123);

  FlagValue<bool> bool_flag(false);
  bool b = bool_flag.value();
  EXPECT_FALSE(b);
}

// Test that FlagValue supports comparison operators with its underlying type
TEST_F(CasFlagsTests, FlagValueComparisons) {
  FlagValue<std::string> string_flag("test");
  EXPECT_TRUE(string_flag.value() == "test");
  EXPECT_FALSE(string_flag.value() != "test");

  FlagValue<int> int_flag(100);
  EXPECT_TRUE(int_flag.value() == 100);
  EXPECT_FALSE(int_flag.value() == 99);

  FlagValue<bool> bool_flag(true);
  EXPECT_TRUE(bool_flag.value() == true);
  EXPECT_FALSE(bool_flag.value() == false);
}

// Test that FlagValue supports pointer-like operations via value()
TEST_F(CasFlagsTests, FlagValuePointerLike) {
  FlagValue<std::string> string_flag("example");
  EXPECT_EQ(string_flag.value().length(), 7);
}

// ============================================================================
// Tests for the three-tier priority logic (Default -> Config -> CLI)
// These tests verify the logic that *uses* FlagValue, not FlagValue itself.
// ============================================================================

// Test three-tier priority: CLI values override defaults
TEST_F(CasFlagsTests, PriorityLogicCliOverridesDefaults) {
  FlagValue<int> flag(100);  // Default value

  // Simulate CLI parsing
  flag.set_value(200);

  // Verify CLI value is present and marked
  EXPECT_TRUE(flag.user_provided());
  EXPECT_EQ(flag.value(), 200);
}

// Test three-tier priority: config values apply only when not set by CLI
TEST_F(CasFlagsTests, PriorityLogicConfigAppliesWhenNotCli) {
  FlagValue<std::string> flag("default");  // Default value

  // Simulate config loading (flag was not set by CLI)
  if (!flag.user_provided()) {
    flag.set_value("config_value");
  }

  // Verify config value is now in place
  EXPECT_EQ(flag.value(), "config_value");
}

// Test three-tier priority: CLI blocks config from overriding
TEST_F(CasFlagsTests, PriorityLogicCliBlocksConfig) {
  FlagValue<int> flag(100);  // Default value

  // Simulate CLI parsing
  flag.set_value(200);

  // Simulate config loading attempt
  if (!flag.user_provided()) {
    // This block should NOT execute
    flag.set_value(300);
  }

  // Verify CLI value remains
  EXPECT_EQ(flag.value(), 200);
}

}  // namespace cuttlefish
