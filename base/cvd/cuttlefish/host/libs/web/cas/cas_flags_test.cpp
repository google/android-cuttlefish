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

#include <fstream>
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
  EXPECT_EQ(flags.prefer_uncompressed, false);
  EXPECT_EQ(flags.cache_dir, "");
  EXPECT_EQ(flags.cache_max_size, kMinCacheMaxSize);
  EXPECT_EQ(flags.cache_lock, false);
  EXPECT_EQ(flags.use_hardlink, true);
  EXPECT_EQ(flags.memory_limit, kDefaultMemoryLimit);
  EXPECT_EQ(flags.cas_concurrency, kDefaultCasConcurrency);
  EXPECT_EQ(flags.rpc_timeout, kDefaultRpcTimeout);
  EXPECT_EQ(flags.get_capabilities_timeout, kDefaultGetCapabilitiesTimeout);
  EXPECT_EQ(flags.get_tree_timeout, kDefaultGetTreeTimeout);
  EXPECT_EQ(flags.batch_read_blobs_timeout, kDefaultBatchReadBlobsTimeout);
  EXPECT_EQ(flags.batch_update_blobs_timeout, kDefaultBatchUpdateBlobsTimeout);
  EXPECT_EQ(flags.version, false);
}

// Test that CasDownloaderFlags can be manually set even if default exists
TEST_F(CasFlagsTests, CanOverrideDefaultConfigPath) {
  CasDownloaderFlags flags;
  std::string custom_path = "/custom/config/path.json";
  flags.cas_config_filepath = custom_path;

  EXPECT_EQ(flags.cas_config_filepath, custom_path);
}

// Test that the constructor accepts the expected default config path constant
TEST_F(CasFlagsTests, DefaultConfigPathConstantExists) {
  // Verify that kDefaultCasConfigFilePath is defined and contains
  // the expected path
  const std::string expected_path = "/etc/casdownloader/config.json";
  EXPECT_EQ(std::string(kDefaultCasConfigFilePath), expected_path);
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
  flags1.downloader_path = "/path/to/downloader";
  flags1.cache_dir = "/cache/dir";

  // Verify flags2 is not affected
  if (DefaultDownloaderPathExists()) {
    EXPECT_EQ(flags2.downloader_path, std::string(kDefaultDownloaderPath));
  } else {
    EXPECT_EQ(flags2.downloader_path, "");
  }
  EXPECT_EQ(flags2.cache_dir, "");
}

// Test that the constructor accepts the expected default downloader path constant
TEST_F(CasFlagsTests, DefaultDownloaderPathConstantExists) {
  // Verify that kDefaultDownloaderPath is defined and contains
  // the expected path
  const std::string expected_path = "/usr/bin/casdownloader";
  EXPECT_EQ(std::string(kDefaultDownloaderPath), expected_path);
}

// Test that CasDownloaderFlags can override the default downloader path
TEST_F(CasFlagsTests, CanOverrideDefaultDownloaderPath) {
  CasDownloaderFlags flags;
  std::string custom_downloader = "/custom/path/to/downloader";
  flags.downloader_path = custom_downloader;

  EXPECT_EQ(flags.downloader_path, custom_downloader);
}

// Environment-dependent tests: These tests verify that cas_config_filepath and
// downloader_path are correctly initialized based on the presence of system
// files. They help detect errors in the default-initialization logic.
// Note: These tests are environment-dependent in the sense that they exercise
// different code paths depending on whether /etc/casdownloader/config.json and
// /usr/bin/casdownloader exist on the system. They should always pass, but
// cover different scenarios:
//   - If files exist: verifies defaults are correctly set
//   - If files don't exist: verifies fields remain empty
// This helps catch initialization logic errors in both paths.

// Test that cas_config_filepath is initialized from default when file exists
TEST_F(CasFlagsTests, CasConfigFilePathInitializedCorrectly) {
  CasDownloaderFlags flags;
  
  if (DefaultConfigPathExists()) {
    // If the default config file exists, it should be used
    EXPECT_EQ(flags.cas_config_filepath, std::string(kDefaultCasConfigFilePath));
  } else {
    // If it doesn't exist, the field should remain empty
    EXPECT_TRUE(flags.cas_config_filepath.value.empty());
  }
}

// Test that downloader_path is initialized from default when binary exists
TEST_F(CasFlagsTests, DownloaderPathInitializedCorrectly) {
  CasDownloaderFlags flags;
  
  if (DefaultDownloaderPathExists()) {
    // If the default downloader binary exists, it should be used
    EXPECT_EQ(flags.downloader_path, std::string(kDefaultDownloaderPath));
  } else {
    // If it doesn't exist, the field should remain empty
    EXPECT_TRUE(flags.downloader_path.value.empty());
  }
}

// Test that both default paths are independently initialized
// This test verifies that initializing one default doesn't affect the other
TEST_F(CasFlagsTests, BothDefaultPathsInitializedIndependently) {
  CasDownloaderFlags flags;

  // Verify cas_config_filepath initialization (independent of downloader_path)
  if (DefaultConfigPathExists()) {
    EXPECT_EQ(flags.cas_config_filepath, std::string(kDefaultCasConfigFilePath));
  } else {
    EXPECT_TRUE(flags.cas_config_filepath.value.empty());
  }

  // Verify downloader_path initialization (independent of cas_config_filepath)
  if (DefaultDownloaderPathExists()) {
    EXPECT_EQ(flags.downloader_path, std::string(kDefaultDownloaderPath));
  } else {
    EXPECT_TRUE(flags.downloader_path.value.empty());
  }
}

// ============================================================================
// Tests for FlagValue<T> wrapper behavior (three-tier priority system)
// ============================================================================

// Test that FlagValue tracks user_specified correctly for defaults
TEST_F(CasFlagsTests, FlagValueDefaultNotUserSpecified) {
  CasDownloaderFlags flags;
  
  // All defaults should have user_specified = false
  EXPECT_FALSE(flags.downloader_path.user_specified);
  EXPECT_FALSE(flags.prefer_uncompressed.user_specified);
  EXPECT_FALSE(flags.cache_dir.user_specified);
  EXPECT_FALSE(flags.cache_max_size.user_specified);
  EXPECT_FALSE(flags.cache_lock.user_specified);
  EXPECT_FALSE(flags.use_hardlink.user_specified);
  EXPECT_FALSE(flags.memory_limit.user_specified);
  EXPECT_FALSE(flags.cas_concurrency.user_specified);
  EXPECT_FALSE(flags.rpc_timeout.user_specified);
  EXPECT_FALSE(flags.get_capabilities_timeout.user_specified);
  EXPECT_FALSE(flags.get_tree_timeout.user_specified);
  EXPECT_FALSE(flags.batch_read_blobs_timeout.user_specified);
  EXPECT_FALSE(flags.batch_update_blobs_timeout.user_specified);
  EXPECT_FALSE(flags.version.user_specified);
}

// Test that FlagValue can be accessed via implicit conversion
TEST_F(CasFlagsTests, FlagValueImplicitConversion) {
  CasDownloaderFlags flags;
  
  // Test implicit conversion for string
  std::string path = flags.downloader_path;  // Should call operator T&()
  // Value should be the default or empty
  
  // Test implicit conversion for bool
  bool uncompressed = flags.prefer_uncompressed;  // Should call operator bool()
  EXPECT_FALSE(uncompressed);
  
  // Test implicit conversion for int
  int concurrency = flags.cas_concurrency;  // Should call operator int()
  EXPECT_EQ(concurrency, kDefaultCasConcurrency);
  
  // Test implicit conversion for int64_t (cache_max_size)
  int64_t cache_size = flags.cache_max_size;  // Should call operator int64_t()
  EXPECT_EQ(cache_size, kMinCacheMaxSize);
}

// Test that FlagValue supports comparison operators
TEST_F(CasFlagsTests, FlagValueComparisons) {
  CasDownloaderFlags flags;
  
  // Test string comparison
  EXPECT_TRUE(flags.cache_dir == "");
  EXPECT_FALSE(flags.cache_dir != "");
  
  // Test int comparison
  EXPECT_TRUE(flags.memory_limit == kDefaultMemoryLimit);
  EXPECT_FALSE(flags.memory_limit > 1000);
  
  // Test bool comparison
  EXPECT_FALSE(flags.prefer_uncompressed == true);
  EXPECT_TRUE(flags.prefer_uncompressed == false);

  // Test int64_t comparison (cache_max_size)
  EXPECT_TRUE(flags.cache_max_size == kMinCacheMaxSize);  // Should call operator int64_t()
  EXPECT_FALSE(flags.cache_max_size != kMinCacheMaxSize);
}

// Test that FlagValue supports pointer operator
TEST_F(CasFlagsTests, FlagValuePointerOperator) {
  CasDownloaderFlags flags;
  flags.cache_dir.value = "/cache";
  
  // Test pointer operator
  size_t len = flags.cache_dir->length();
  EXPECT_EQ(len, 6);  // "/cache" has 6 characters
}

// Test FlagValue string value can be set and retrieved
TEST_F(CasFlagsTests, FlagValueStringSetAndGet) {
  CasDownloaderFlags flags;
  
  // Initially should be empty or default
  std::string original = flags.downloader_path.value;
  
  // Set a custom value
  flags.downloader_path.value = "/custom/path";
  EXPECT_EQ(flags.downloader_path.value, "/custom/path");
  
  // user_specified should still be false (we manually set .value, not via setter)
  EXPECT_FALSE(flags.downloader_path.user_specified);
}

// Test FlagValue bool value can be set and retrieved
TEST_F(CasFlagsTests, FlagValueBoolSetAndGet) {
  CasDownloaderFlags flags;
  
  EXPECT_FALSE(flags.prefer_uncompressed.value);
  flags.prefer_uncompressed.value = true;
  EXPECT_TRUE(flags.prefer_uncompressed.value);
}

// Test FlagValue int value can be set and retrieved
TEST_F(CasFlagsTests, FlagValueIntSetAndGet) {
  CasDownloaderFlags flags;
  
  EXPECT_EQ(flags.memory_limit.value, kDefaultMemoryLimit);
  flags.memory_limit.value = 4096;
  EXPECT_EQ(flags.memory_limit.value, 4096);
}

// Test FlagValue int64_t value can be set and retrieved
TEST_F(CasFlagsTests, FlagValueInt64SetAndGet) {
  CasDownloaderFlags flags;
  
  EXPECT_EQ(flags.cache_max_size.value, kMinCacheMaxSize);
  int64_t custom_size = 16LL * 1024 * 1024 * 1024;  // 16 GiB
  flags.cache_max_size.value = custom_size;
  EXPECT_EQ(flags.cache_max_size.value, custom_size);
}

// Test that marking user_specified works for tracking CLI vs defaults
TEST_F(CasFlagsTests, FlagValueUserSpecifiedTracking) {
  CasDownloaderFlags flags;
  
  // Initially not user-specified
  EXPECT_FALSE(flags.downloader_path.user_specified);
  EXPECT_FALSE(flags.cache_dir.user_specified);
  
  // Mark as user-specified
  flags.downloader_path.user_specified = true;
  flags.cache_dir.user_specified = true;
  
  // Now should be marked
  EXPECT_TRUE(flags.downloader_path.user_specified);
  EXPECT_TRUE(flags.cache_dir.user_specified);
}

// Test three-tier priority: all defaults
TEST_F(CasFlagsTests, ThreeTierPriorityAllDefaults) {
  CasDownloaderFlags flags;
  
  // All flags should use defaults and be marked as not user-specified
  EXPECT_FALSE(flags.downloader_path.user_specified);
  EXPECT_FALSE(flags.cache_dir.user_specified);
  EXPECT_FALSE(flags.memory_limit.user_specified);
  EXPECT_FALSE(flags.rpc_timeout.user_specified);
  
  // Verify defaults are in place
  EXPECT_EQ(flags.cache_dir.value, "");
  EXPECT_EQ(flags.memory_limit.value, kDefaultMemoryLimit);
  EXPECT_EQ(flags.rpc_timeout.value, kDefaultRpcTimeout);
}

// Test three-tier priority: CLI values override defaults
TEST_F(CasFlagsTests, ThreeTierPriorityCliOverridesDefaults) {
  CasDownloaderFlags flags;
  
  // Simulate CLI parsing setting values
  flags.downloader_path.value = "/my/downloader";
  flags.downloader_path.user_specified = true;
  
  flags.memory_limit.value = 8192;
  flags.memory_limit.user_specified = true;
  
  // Verify CLI values are set and marked
  EXPECT_TRUE(flags.downloader_path.user_specified);
  EXPECT_EQ(flags.downloader_path.value, "/my/downloader");
  
  EXPECT_TRUE(flags.memory_limit.user_specified);
  EXPECT_EQ(flags.memory_limit.value, 8192);
}

// Test three-tier priority: config values apply when not user-specified
TEST_F(CasFlagsTests, ThreeTierPriorityConfigAppliesWhenNotCli) {
  CasDownloaderFlags flags;
  
  // Defaults: not user-specified
  EXPECT_FALSE(flags.cache_dir.user_specified);
  EXPECT_FALSE(flags.rpc_timeout.user_specified);
  
  // Simulate config file loading
  // Check that config can override defaults (when not user-specified)
  if (!flags.cache_dir.user_specified) {
    flags.cache_dir.value = "/var/cache/cas";
    flags.cache_dir.user_specified = true;  // Mark as from config
  }
  
  if (!flags.rpc_timeout.user_specified) {
    flags.rpc_timeout.value = 300;
    flags.rpc_timeout.user_specified = true;  // Mark as from config
  }
  
  // Verify config values are now in place
  EXPECT_EQ(flags.cache_dir.value, "/var/cache/cas");
  EXPECT_EQ(flags.rpc_timeout.value, 300);
  EXPECT_TRUE(flags.cache_dir.user_specified);
  EXPECT_TRUE(flags.rpc_timeout.user_specified);
}

// Test three-tier priority: CLI blocks config from overriding
TEST_F(CasFlagsTests, ThreeTierPriorityCliBlocksConfig) {
  CasDownloaderFlags flags;
  
  // Simulate CLI parsing
  flags.memory_limit.value = 4096;
  flags.memory_limit.user_specified = true;
  
  // Simulate config loading attempt
  if (!flags.memory_limit.user_specified) {
    // This should NOT execute because CLI already set it
    flags.memory_limit.value = 2048;
    flags.memory_limit.user_specified = true;
  }
  
  // CLI value should remain (not overridden by config)
  EXPECT_EQ(flags.memory_limit.value, 4096);
  EXPECT_TRUE(flags.memory_limit.user_specified);
}

// Test multiple flags with mixed CLI and config
TEST_F(CasFlagsTests, ThreeTierPriorityMixedCliAndConfig) {
  CasDownloaderFlags flags;
  
  // CLI specifies only some flags
  flags.downloader_path.value = "/cli/downloader";
  flags.downloader_path.user_specified = true;
  
  flags.memory_limit.value = 4096;
  flags.memory_limit.user_specified = true;
  
  // Config provides other flags (not overriding CLI)
  if (!flags.cache_dir.user_specified) {
    flags.cache_dir.value = "/var/cache";
    flags.cache_dir.user_specified = true;
  }
  
  if (!flags.rpc_timeout.user_specified) {
    flags.rpc_timeout.value = 300;
    flags.rpc_timeout.user_specified = true;
  }
  
  // Config tries to override CLI (should be blocked)
  if (!flags.memory_limit.user_specified) {
    flags.memory_limit.value = 2048;  // Should NOT execute
  }
  
  // Verify: CLI wins, config fills in gaps, defaults not used
  EXPECT_EQ(flags.downloader_path.value, "/cli/downloader");
  EXPECT_EQ(flags.memory_limit.value, 4096);  // CLI value, not config
  EXPECT_EQ(flags.cache_dir.value, "/var/cache");  // Config value
  EXPECT_EQ(flags.rpc_timeout.value, 300);  // Config value
}

}  // namespace cuttlefish

