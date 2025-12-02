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

#include "cuttlefish/host/libs/web/cas/cas_downloader.h"

#include <android-base/strings.h>
#include <stdint.h>

#include <cstddef>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <android-base/expected.h>
#include <android-base/logging.h>
#include <json/value.h>

#include "absl/strings/match.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/cas/cas_flags.h"

namespace cuttlefish {

namespace {

std::set<std::string> GetSupportedFlags(const std::string& go_binary_path) {
  std::set<std::string> flags;

  Command command(go_binary_path);
  command.AddParameter("-help");
  std::string out;
  std::string err;
  RunWithManagedStdio(std::move(command), nullptr, &out, &err);
  // "casdownloader -help" outputs to stderr.
  std::vector<std::string> lines = android::base::Tokenize(err, "\n");
  for (std::string& line : lines) {
    // Sample help output line: "  -version=false: Print version information"
    if (line.find("  -") != std::string::npos) {
      size_t start = line.find("  -") + 3;
      size_t end = line.find('=', start);
      if (end == std::string::npos) {
        end = line.size();
      }
      if (end > start) {
        flags.insert(line.substr(start, end - start));
      }
    }
  }

  return flags;
}

std::vector<std::string> CreateCasFlags(std::string downloader_path,
                                        Json::Value& config_flags) {
  std::vector<std::string> cas_flags;

  // If cache-dir is set and cache-max-size is absent or smaller than the
  // default, ensure cache-max-size is at least kMinCacheMaxSize.
  if (!config_flags["cache-dir"].asString().empty()) {
    if (config_flags.isMember("cache-max-size")) {
      int64_t provided = config_flags["cache-max-size"].asInt64();
      if (provided < kMinCacheMaxSize) {
        LOG(WARNING) << "cache-max-size (" << provided
                     << ") is smaller than default; using default ("
                     << kMinCacheMaxSize << ")";
        config_flags["cache-max-size"] = kMinCacheMaxSize;
      }
    } else {
      config_flags["cache-max-size"] = kMinCacheMaxSize;
    }
  }

  // Releasing of casdownloader and cvd can be out of sync. Filter out
  // unsupported flags for casdownloader.
  std::set<std::string> supported_flags = GetSupportedFlags(downloader_path);
  auto is_supported = [&supported_flags](std::string flag) {
    return supported_flags.find(flag) != supported_flags.end();
  };

  std::set<std::string> auto_populated_flags = {
      kFlagCasInstance, kFlagCasAddr,      kFlagDigest,
      kFlagDir,         kFlagDisableCache, kFlagServiceAccountJson,
      kFlagUseAdc};
  auto is_autopopulated = [&auto_populated_flags](std::string flag) {
    return auto_populated_flags.find(flag) != auto_populated_flags.end();
  };

  std::set<std::string> bool_flags{"cache-lock", "use-hardlink", "version"};
  auto is_bool_flag = [&bool_flags](std::string flag) {
    return bool_flags.find(flag) != bool_flags.end();
  };

  std::vector<std::string> ignored_flags{};
  std::map<std::string, std::string> rpc_options{
      {"memory-limit", std::to_string(kDefaultMemoryLimit)},
      {"cas-concurrency", std::to_string(kDefaultCasConcurrency)},
      {"rpc-timeout", std::to_string(kDefaultRpcTimeout) + "s"},
      {"get-capabilities-timeout",
       std::to_string(kDefaultGetCapabilitiesTimeout) + "s"},
      {"get-tree-timeout", std::to_string(kDefaultGetTreeTimeout) + "s"},
      {"batch-read-blobs-timeout",
       std::to_string(kDefaultBatchReadBlobsTimeout) + "s"},
      {"batch-update-blobs-timeout",
       std::to_string(kDefaultBatchUpdateBlobsTimeout) + "s"}};
  auto is_rpc_option = [&rpc_options](std::string flag) {
    return rpc_options.find(flag) != rpc_options.end();
  };

  for (const std::string& flag : config_flags.getMemberNames()) {
    if (!is_supported(flag) || is_autopopulated(flag)) {
      ignored_flags.push_back(flag);
      continue;
    }
    if (is_rpc_option(flag)) {
      rpc_options.erase(flag);
    }
    std::string flag_str = "-" + flag;
    if (!is_bool_flag(flag)) {
      flag_str += "=" + config_flags[flag].asString();
    } else if (!config_flags[flag].asBool()) {
      flag_str += "=false";
    }
    cas_flags.push_back(flag_str);
  }
  for (const auto& [flag, value] : rpc_options) {
    if (is_supported(flag)) {
      std::string flag_str = "-" + flag + "=" + value;
      cas_flags.push_back(flag_str);
    }
  }

  if (config_flags["cache-dir"].asString().empty()) {
    cas_flags.push_back("-" + std::string(kFlagDisableCache));
  }

  if (!ignored_flags.empty()) {
    LOG(WARNING) << "CAS Downloader flags ignored: '"
                 << android::base::Join(ignored_flags, "', '") << "'";
  }

  return cas_flags;
}

inline std::string ToSeconds(int timeout) {
  return std::to_string(timeout) + "s";
}

Json::Value ConvertToConfigFlags(const CasDownloaderFlags& flags) {
  Json::Value config_flags;
  config_flags["cache-dir"] = flags.cache_dir.value();
  config_flags["cache-max-size"] = flags.cache_max_size.value();
  config_flags["cache-lock"] = flags.cache_lock.value();
  config_flags["use-hardlink"] = flags.use_hardlink.value();
  config_flags["cas-concurrency"] = flags.cas_concurrency.value();
  config_flags["memory-limit"] = flags.memory_limit.value();
  config_flags["rpc-timeout"] = ToSeconds(flags.rpc_timeout.value());
  config_flags["get-capabilities-timeout"] =
      ToSeconds(flags.get_capabilities_timeout.value());
  config_flags["get-tree-timeout"] = ToSeconds(flags.get_tree_timeout.value());
  config_flags["batch-read-blobs-timeout"] =
      ToSeconds(flags.batch_read_blobs_timeout.value());
  config_flags["batch-update-blobs-timeout"] =
      ToSeconds(flags.batch_update_blobs_timeout.value());
  config_flags["version"] = flags.version.value();
  config_flags["invocation-id"] = flags.invocation_id.value();
  return config_flags;
}

Command GetCommand(const std::string downloader_path,
                   const std::vector<std::string>& flags,
                   const CasIdentifier& cas_identifier,
                   const std::string& target_directory,
                   const std::optional<std::string>& stats_filepath) {
  Command cmd(downloader_path);
  cmd.AddParameter("-", std::string(kFlagCasInstance),
                   "=" + cas_identifier.cas_instance);
  cmd.AddParameter("-", std::string(kFlagCasAddr),
                   "=" + cas_identifier.cas_addr);
  cmd.AddParameter("-", std::string(kFlagDigest), "=" + cas_identifier.digest);
  cmd.AddParameter("-", std::string(kFlagDir), "=", target_directory);
  if (stats_filepath.has_value()) {
    cmd.AddParameter("-", std::string(kFlagDumpJson), "=", *stats_filepath);
  }
  for (const std::string& flag : flags) {
    cmd.AddParameter(flag);
  }
  return cmd;
}

// Helper function to merge CLI values into config flags when CLI takes
// precedence. This function implements the priority rule: if a flag was
// specified on the CLI (user_provided()=true), its value overrides any value
// from the config file.
void MergeCliValuesIntoConfig(const CasDownloaderFlags& flags,
                              Json::Value& config_flags) {
  // Merge CLI values (if provided) on top of config file values so CLI wins.
  // Use the same keys as ConvertToConfigFlags.

  if (flags.cache_dir.user_provided()) {
    config_flags["cache-dir"] = flags.cache_dir.value();
  }
  if (flags.invocation_id.user_provided()) {
    config_flags["invocation-id"] = flags.invocation_id.value();
  }
  if (flags.cache_max_size.user_provided()) {
    config_flags["cache-max-size"] = flags.cache_max_size.value();
  }
  if (flags.cache_lock.user_provided()) {
    config_flags["cache-lock"] = flags.cache_lock.value();
  }
  if (flags.use_hardlink.user_provided()) {
    config_flags["use-hardlink"] = flags.use_hardlink.value();
  }
  if (flags.cas_concurrency.user_provided()) {
    config_flags["cas-concurrency"] = flags.cas_concurrency.value();
  }
  if (flags.memory_limit.user_provided()) {
    config_flags["memory-limit"] = flags.memory_limit.value();
  }
  if (flags.rpc_timeout.user_provided()) {
    config_flags["rpc-timeout"] = ToSeconds(flags.rpc_timeout.value());
  }
  if (flags.get_capabilities_timeout.user_provided()) {
    config_flags["get-capabilities-timeout"] =
        ToSeconds(flags.get_capabilities_timeout.value());
  }
  if (flags.get_tree_timeout.user_provided()) {
    config_flags["get-tree-timeout"] =
        ToSeconds(flags.get_tree_timeout.value());
  }
  if (flags.batch_read_blobs_timeout.user_provided()) {
    config_flags["batch-read-blobs-timeout"] =
        ToSeconds(flags.batch_read_blobs_timeout.value());
  }
  if (flags.batch_update_blobs_timeout.user_provided()) {
    config_flags["batch-update-blobs-timeout"] =
        ToSeconds(flags.batch_update_blobs_timeout.value());
  }
  if (flags.version.user_provided()) {
    config_flags["version"] = flags.version.value();
  }
}

Result<bool> HasConfigFile(const FlagValue<std::string>& config_filepath_flag) {
  // Determine whether there's a config file to load. The
  // `cas_config_filepath` may contain a default path (empty if none). If the
  // user explicitly provided a config filepath and it does not exist, that's
  // an error. If a config file exists (either user-provided or default),
  // we'll load it and apply its values unless the corresponding CLI flag was
  // provided.
  const std::string& config_filepath = config_filepath_flag.value();
  if (config_filepath.empty()) {
    return false;
  }
  if (FileExists(config_filepath)) {
    return true;
  }
  if (config_filepath_flag.user_provided()) {
    return CF_ERRF("CAS Config file not found: {}", config_filepath);
  }
  return false;  // Path was a default, but file doesn't exist.
}

}  // namespace

Result<std::unique_ptr<CasDownloader>> CasDownloader::Create(
    const CasDownloaderFlags& cas_downloader_flags,
    const std::string& service_account_filepath) {
  auto result = CreateImpl(cas_downloader_flags, service_account_filepath);
  if (result.ok()) {
    return result;
  }
  // Ensure callers and logs clearly indicate that CAS downloading is
  // disabled and why, using the same environment-aware formatting that
  // test helpers use.
  LOG(INFO) << "CAS downloading disabled: " << result.error().FormatForEnv();
  return result;
}

Result<std::unique_ptr<CasDownloader>> CasDownloader::CreateImpl(
    const CasDownloaderFlags& cas_downloader_flags,
    const std::string& service_account_filepath) {
  // Start with values from the FlagValue wrappers (these contain defaults
  // and reflect any CLI-provided values via .user_provided()).
  std::string downloader_path = cas_downloader_flags.downloader_path.value();
  bool prefer_uncompressed = cas_downloader_flags.prefer_uncompressed.value();
  std::vector<std::string> cas_flags;

  Json::Value config_flags;
  bool has_config_file =
      CF_EXPECT(HasConfigFile(cas_downloader_flags.cas_config_filepath));
  if (has_config_file) {
    // A config file exists. Load it, then merge CLI values on top so
    // that CLI takes precedence.
    const std::string& config_filepath =
        cas_downloader_flags.cas_config_filepath.value();
    bool is_default_config = (config_filepath == kDefaultCasConfigFilePath);
    if (is_default_config) {
      LOG(INFO) << "Using default CAS config from: " << config_filepath;
    } else {
      LOG(INFO) << "Using CAS config from: " << config_filepath;
    }
    std::string config_contents = CF_EXPECT(ReadFileContents(config_filepath));
    Json::Value config = CF_EXPECT(ParseJson(config_contents));

    // Base config flags from file (may be empty object)
    config_flags = config[kKeyFlags];

    // downloader-path and prefer-uncompressed are top-level in the config.
    // Apply them only if not provided on the CLI.
    if (!cas_downloader_flags.downloader_path.user_provided()) {
      if (config.isMember(kKeyDownloaderPath)) {
        downloader_path = config[kKeyDownloaderPath].asString();
      }
    }
    if (!cas_downloader_flags.prefer_uncompressed.user_provided()) {
      if (config.isMember("prefer-uncompressed")) {
        prefer_uncompressed = config["prefer-uncompressed"].asBool();
      }
    }

    // For each supported flag key we merge CLI values (if provided) on top of
    // the config file values so CLI wins. Use the same keys as
    // ConvertToConfigFlags.
    MergeCliValuesIntoConfig(cas_downloader_flags, config_flags);

    // If the config file didn't provide a downloader path and CLI didn't as
    // well, this will be caught below when we require a non-empty path.
  } else {
    // No config file available: use CLI values (or defaults if CLI didn't set
    // them). Convert current flag values into config_flags for downstream
    // processing.
    LOG(INFO) << "Using CAS downloader flags from command line or defaults.";
    config_flags = ConvertToConfigFlags(cas_downloader_flags);
  }

  // Final sanity: ensure we have a downloader path and binary exists.
  CF_EXPECT(!downloader_path.empty(),
            "CAS downloader path not provided. Use --cas_downloader_path or "
            "set downloader-path in config file.");
  CF_EXPECT(FileExists(downloader_path),
            "CAS Downloader binary not found at: " << downloader_path);

  // Create cas_flags from the merged config_flags (CLI-overrides applied
  // above)
  cas_flags = CreateCasFlags(downloader_path, config_flags);

  if (!service_account_filepath.empty() &&
      FileExists(service_account_filepath)) {
    cas_flags.push_back("-" + std::string(kFlagServiceAccountJson) + "=" +
                        std::string(service_account_filepath));
  } else {
    cas_flags.push_back("-" + std::string(kFlagUseAdc));
  }

  return std::unique_ptr<CasDownloader>(
      new CasDownloader{downloader_path, cas_flags, prefer_uncompressed});
}

void AppendBuildInfoToInvocationId(const DeviceBuild& build,
                                   std::vector<std::string>& cas_flags) {
  // Append build info, including build id, branch, and flavor, to the
  // invocation-id flag. Do this only if the invocation-id flag is already
  // present and contains `caller` only.
  for (auto& flag : cas_flags) {
    if (absl::StartsWith(flag, "-invocation-id=caller=") &&
        flag.find(',') == std::string::npos) {
      if (!build.id.empty()) {
        flag += ",bid=" + build.id;
      }
      if (!build.branch.empty()) {
        flag += ",branch=" + build.branch;
      }
      if (!build.target.empty()) {
        flag += ",flavor=" + build.target;
      }
      return;
    }
  }
}

CasDownloader::CasDownloader(std::string downloader_path,
                             std::vector<std::string> flags,
                             bool prefer_uncompressed)
    : downloader_path_(std::move(downloader_path)),
      flags_(std::move(flags)),
      prefer_uncompressed_(prefer_uncompressed) {}

Result<void> CasDownloader::DownloadFile(
    const DeviceBuild& build, const std::string& artifact_name,
    const std::string& target_directory, const DigestsFetcher& digests_fetcher,
    const std::optional<std::string>& stats_filepath) {
  std::string download_directory = target_directory;
  // Extract build id and target from DeviceBuild
  const std::string& build_id = build.id;
  const std::string& build_target = build.target;
  CasIdentifier cas_identifier = CF_EXPECT(
      GetCasIdentifier(build_id, build_target, artifact_name, digests_fetcher));
  std::string filename = cas_identifier.filename;
  // Uncompressed artifacts have the prefix "_chunked_dir_".
  if (filename.find("_chunked_dir_") == 0) {
    download_directory += "/" + artifact_name;
  }
  AppendBuildInfoToInvocationId(build, flags_);
  Command cmd = GetCommand(downloader_path_, flags_, cas_identifier,
                           download_directory, stats_filepath);
  LOG(INFO) << "CAS Downloader Command: '" << cmd.AsBashScript() << "'";
  int ret_code = cmd.Start().Wait();
  if (ret_code != 0) {
    return CF_ERRF("Failed to download file with CAS downloader ({}).",
                   ret_code);
  };
  CF_EXPECT(FileExists(target_directory + "/" + artifact_name),
            "Failed to download file with CAS downloader.");
  return {};
}

inline std::string ConstructBuildDesc(const std::string& build_id,
                                      const std::string& build_target) {
  return build_id + ":" + build_target;
}

Result<CasIdentifier> CasDownloader::GetCasIdentifier(
    const std::string& build_id, const std::string& build_target,
    const std::string& artifact_name, const DigestsFetcher& digests_fetcher) {
  std::string build_desc = ConstructBuildDesc(build_id, build_target);
  if (build_desc != build_desc_) {
    const std::string digests_filename = "cas_digests.json";
    std::string digests_filepath = CF_EXPECT(digests_fetcher(digests_filename));
    Json::Value cas_digests = CF_EXPECT(ParseJson(ReadFile(digests_filepath)));
    RemoveFile(digests_filepath);
    std::vector<std::string> mandatory_keys{
        "cas_instance",
        "cas_service",
        "files",
    };
    for (const std::string& key : mandatory_keys) {
      CF_EXPECTF(cas_digests.isMember(key),
                 "cas_digests.json corrupted, missing the '{}' field", key);
    }
    build_desc_ = std::move(build_desc);
    cas_digests_ = std::move(cas_digests);
  }
  std::vector<std::string> artifact_prefixes = {
      "_chunked_",
      "",
  };
  if (prefer_uncompressed_) {
    artifact_prefixes.insert(artifact_prefixes.begin(), "_chunked_dir_");
  }
  for (const std::string& artifact_prefix : artifact_prefixes) {
    const std::string filename = artifact_prefix + artifact_name;
    if (cas_digests_["files"].isMember(filename)) {
      std::string digest = cas_digests_["files"][filename].asString();
      return CasIdentifier{cas_digests_["cas_instance"].asString(),
                           cas_digests_["cas_service"].asString(), digest,
                           filename};
    }
  }
  return CF_ERRF("CAS digest for '{}' not found.", artifact_name);
}

}  // namespace cuttlefish
