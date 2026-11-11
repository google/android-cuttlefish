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

#include <stdlib.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "android-base/file.h"
#include "fmt/base.h"
#include "fmt/core.h"
#include "fmt/format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string/device_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/web/cas/cas_flags.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

std::string CreateTempFileWithText(const std::string& filepath,
                                   const std::string& text) {
  std::ofstream file(filepath);
  file << text;
  file.close();
  return filepath;
}

namespace {

Result<void> RemoveFileIfExists(const std::string& path) {
  if (FileExists(path)) {
    CF_EXPECT(RemoveFile(path));
  }
  return {};
}

// Takes the name of the requested file, returns the path it was written to.
using DigestsFn = std::function<Result<std::string>(const std::string&)>;

// Stands in for the Android Build API as the source of the digests file.
class FakeDigestsSource : public BuildApi<DeviceBuildString, DeviceBuild> {
 public:
  Result<DeviceBuild> GetBuild(const DeviceBuildString&) override {
    return CF_ERR("not used");
  }

  Result<std::string> DownloadFile(const DeviceBuild&,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override {
    requests.push_back(target_directory + "/" + artifact_name);
    CF_EXPECT(fetch != nullptr, "No digests set up");
    return CF_EXPECT(fetch(artifact_name));
  }

  Result<SeekableZipSource> FileReader(const DeviceBuild&,
                                       const std::string&) override {
    return CF_ERR("not used");
  }

  DigestsFn fetch;
  std::vector<std::string> requests;
};

}  // namespace

class CasDownloaderTests : public ::testing::Test {
 protected:
  std::string FakeCasdownloader(const std::string& text) {
    std::string fake_casdownloader_filepath = CreateTempFileWithText(
        std::string(temp_dir_.path) + "/casdownloader", text);
    MakeFileExecutable(fake_casdownloader_filepath);
    return fake_casdownloader_filepath;
  }

  template <typename... Args>
  std::string CreateCasConfig(const std::string& downloader_path,
                              bool prefer_uncompressed, Args... flag_args) {
    std::string flags_string = "";
    std::vector<std::string> flags = {flag_args...};
    bool first_flag = true;
    for (const std::string& flag : flags) {
      if (first_flag) {
        first_flag = false;
      } else {
        flags_string += ",";
      }
      size_t pos = flag.find('=');
      if (pos != std::string::npos) {
        std::string name = flag.substr(0, pos);
        std::string value = flag.substr(pos + 1);
        if (value == "false") {
          flags_string += fmt::format("\n      \"{}\": false", name);
        } else {
          // Emit pure-digit values as JSON numbers so jsoncpp asInt64() works.
          bool is_number = !value.empty() &&
                           std::all_of(value.begin(), value.end(), ::isdigit);
          if (is_number) {
            flags_string += fmt::format("\n      \"{}\": {}", name, value);
          } else {
            flags_string += fmt::format("\n      \"{}\": \"{}\"", name, value);
          }
        }
      } else {
        flags_string += fmt::format("\n      \"{}\": true", flag);
      }
    }
    std::string prefer_uncompressed_string = "";
    if (prefer_uncompressed) {
      prefer_uncompressed_string = R"("prefer-uncompressed": true,)";
    }
    std::string text =
        fmt::format(R"({{
    "downloader-path": "{DOWNLOADER_PATH}",
    {PREFER_UNCOMPRESSED}
    "flags": {{
      {FLAGS}
    }}
}})",
                    fmt::arg("DOWNLOADER_PATH", downloader_path),
                    fmt::arg("PREFER_UNCOMPRESSED", prefer_uncompressed_string),
                    fmt::arg("FLAGS", flags_string));
    return CreateTempFileWithText(
        std::string(temp_dir_.path) + "/cas_config.json", text);
  }

  std::unique_ptr<CasDownloader> CasFromCommandLine(
      std::string service_account_filepath, CasDownloaderFlags flags) {
    Result<std::unique_ptr<CasDownloader>> result =
        CasDownloader::Create(flags, service_account_filepath, digests_source_);
    if (result.has_value()) {
      return std::move(result.value());
    }
    return nullptr;
  }

  template <typename... Args>
  std::unique_ptr<CasDownloader> CasWithServiceAccountUsingConfig(
      std::string service_account_filepath, bool prefer_uncompressed = false,
      Args... flag_args) {
    std::string cas_config_filepath =
        CreateCasConfig(downloader_path_, prefer_uncompressed, flag_args...);
    CasDownloaderFlags flags;
    flags.cas_config_filepath.set_value(cas_config_filepath);
    Result<std::unique_ptr<CasDownloader>> result =
        CasDownloader::Create(flags, service_account_filepath, digests_source_);
    if (result.has_value()) {
      return std::move(result.value());
    }
    return nullptr;
  }

  template <typename... Args>
  std::unique_ptr<CasDownloader> CasUsingConfig(Args... flag_args) {
    return CasWithServiceAccountUsingConfig("", false, flag_args...);
  }

  template <typename... Args>
  std::unique_ptr<CasDownloader> CasPreferUncompressedUsingConfig(
      Args... flag_args) {
    return CasWithServiceAccountUsingConfig("", true, flag_args...);
  }

  template <typename... Args>
  std::string FakeDownloaderForArtifactAndFlags(std::string artifact_path,
                                                Args... flag_args) {
    std::vector<std::string> flags = {flag_args...};
    std::string flags_string = "";
    for (const std::string& flag : flags) {
      flags_string += fmt::format("echo \"  -{}=...\" >&2\n  ", flag);
    }
    std::string create_artifact_string = "touch " + artifact_path;
    std::string script =
        fmt::format(R"(#!/usr/bin/env bash
if [[ "$1" == "-help" ]]; then
  echo "Usage of casdownloader:" >&2
  {FLAGS}
else
  {CREATE_ARTIFACT}
  rm -rf {CAS_OUTPUT_FILEPATH}
  echo $@ > {CAS_OUTPUT_FILEPATH}
fi
)",
                    fmt::arg("FLAGS", flags_string),
                    fmt::arg("CREATE_ARTIFACT", create_artifact_string),
                    fmt::arg("CAS_OUTPUT_FILEPATH", cas_output_filepath_));
    return FakeCasdownloader(script);
  }

  template <typename... Args>
  std::string CasDigestsFile(const std::string& filename, Args... file_args) {
    std::vector<std::string> files = {file_args...};
    std::string files_string = "";
    bool first_file = true;
    for (const std::string& file : files) {
      if (first_file) {
        first_file = false;
      } else {
        files_string += ",";
      }
      size_t pos = file.find('=');
      if (pos != std::string::npos) {
        std::string filename = file.substr(0, pos);
        std::string digest = file.substr(pos + 1);
        files_string += fmt::format("\n      \"{}\": \"{}\"", filename, digest);
      } else {
        files_string += fmt::format("\n      \"{}\": \"digest\"", filename);
      }
    }

    std::string text = fmt::format(R"({{
    "cas_instance": "cas_instance",
    "cas_service": "cas_addr",
    "files": {{
      {FILES_STRING}
    }}
}})",
                                   fmt::arg("FILES_STRING", files_string));
    return (CreateTempFileWithText(std::string(temp_dir_.path) + "/" + filename,
                                   text));
  }

  DeviceBuild MakeDeviceBuild(const std::string& id, const std::string& target,
                              const std::string& branch = "") {
    DeviceBuild b;
    b.id = id;
    b.target = target;

    if (!branch.empty()) {
      b.branch = branch;
    }

    return b;
  }

  // Downloads `artifact_name` into `target_dir_`, with `digests` providing the
  // digests file.
  Result<std::string> DownloadWithDigests(
      CasDownloader& cas, const DeviceBuild& build,
      const std::string& artifact_name, DigestsFn digests,
      const std::optional<std::string>& stats_filepath = std::nullopt) {
    digests_source_.fetch = std::move(digests);
    if (stats_filepath.has_value()) {
      return CF_EXPECT(
          cas.DownloadFile(build, target_dir_, artifact_name, stats_filepath));
    }
    return CF_EXPECT(cas.DownloadFile(build, target_dir_, artifact_name));
  }

  Result<void> CreateTestDirs() {
    target_dir_ = std::string(temp_dir_.path) + "/target_dir";
    CF_EXPECT(EnsureDirectoryExists(target_dir_, 0755));
    cas_output_filepath_ = std::string(temp_dir_.path) + "/cas_output";
    return {};
  }

  void SetUp() override {
    Result<void> result = CreateTestDirs();
    if (!result.has_value()) {
      FAIL() << result.error();
    }
  }

  TemporaryDir temp_dir_;
  FakeDigestsSource digests_source_;
  std::string target_dir_;
  std::string cas_output_filepath_;
  std::string cas_config_filepath_;
  std::string downloader_path_;
  std::string service_account_filepath_;
};

using testing::ElementsAre;
using testing::Eq;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::IsFalse;
using testing::IsTrue;
using testing::Not;

TEST_F(CasDownloaderTests, FailsToCreateWithInvalidConfigPath) {
  CasDownloaderFlags flags;
  flags.cas_config_filepath.set_value("invalid_config_path");
  Result<std::unique_ptr<CasDownloader>> result =
      CasDownloader::Create(flags, "", digests_source_);

  EXPECT_THAT(result, IsError());
}

TEST_F(CasDownloaderTests, FailsToCreateWithInvalidCasDownloaderPath) {
  downloader_path_ = "invalid_downloader_path";
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();

  EXPECT_THAT(cas, Eq(nullptr));
}

TEST_F(CasDownloaderTests, UsesAdcIfServiceAccountUnavailable) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("-use-adc"));
  EXPECT_THAT(output, Not(HasSubstr("-service-account-json")));
}

TEST_F(CasDownloaderTests, UsesServiceAccountIfAvailable) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  service_account_filepath_ = CreateTempFileWithText(
      std::string(temp_dir_.path) + "/service_account.json", "service_account");
  std::unique_ptr<CasDownloader> cas =
      CasWithServiceAccountUsingConfig(service_account_filepath_, false);

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, Not(HasSubstr("-use-adc")));
  EXPECT_THAT(output, HasSubstr("-service-account-json"));
}

TEST_F(CasDownloaderTests, UsesServiceAccountFromConfig) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  const std::string config_service_account_filepath = CreateTempFileWithText(
      std::string(temp_dir_.path) + "/config_sa.json", "service_account");
  std::unique_ptr<CasDownloader> cas =
      CasUsingConfig("service-account-json=" + config_service_account_filepath);
  ASSERT_THAT(cas, Not(Eq(nullptr)));

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, Not(HasSubstr("-use-adc")));
  EXPECT_THAT(output, HasSubstr("-service-account-json=" +
                                config_service_account_filepath));
}

TEST_F(CasDownloaderTests, ConfigServiceAccountOverridesCommandLine) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  const std::string config_service_account_filepath = CreateTempFileWithText(
      std::string(temp_dir_.path) + "/config_sa.json", "service_account");
  service_account_filepath_ = CreateTempFileWithText(
      std::string(temp_dir_.path) + "/cli_sa.json", "service_account");
  std::unique_ptr<CasDownloader> cas = CasWithServiceAccountUsingConfig(
      service_account_filepath_, false,
      "service-account-json=" + config_service_account_filepath);
  ASSERT_THAT(cas, Not(Eq(nullptr)));

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, Not(HasSubstr("-use-adc")));
  EXPECT_THAT(output, HasSubstr("-service-account-json=" +
                                config_service_account_filepath));
  EXPECT_THAT(output, Not(HasSubstr(service_account_filepath_)));
}

TEST_F(CasDownloaderTests, FailsToCreateWithMissingConfigServiceAccount) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas =
      CasUsingConfig("service-account-json=" + std::string(temp_dir_.path) +
                     "/does_not_exist.json");

  EXPECT_THAT(cas, Eq(nullptr));
}

TEST_F(CasDownloaderTests, IgnoresUnsupportedFlags) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig("unsupported-flag=value");

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, Not(HasSubstr("-unsupported-flag")));
}

TEST_F(CasDownloaderTests, HandlesFalseBoolFlag) {
  downloader_path_ = FakeDownloaderForArtifactAndFlags(
      target_dir_ + "/artifact-img-name", "use-hardlink");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig("use-hardlink=false");

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("-use-hardlink=false"));
}

TEST_F(CasDownloaderTests, HandlesInvocationIdFlag) {
  downloader_path_ = FakeDownloaderForArtifactAndFlags(
      target_dir_ + "/artifact-img-name", "invocation-id");
  std::unique_ptr<CasDownloader> cas =
      CasUsingConfig("invocation-id=caller=caller");

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target", "branch"),
      "artifact-img-name", [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("-invocation-id=caller=caller,bid=build_id,"
                                "branch=branch,flavor=build_target"));
}

TEST_F(CasDownloaderTests, HandlesUnexpectedHelpOutput) {
  std::string script = fmt::format(
      R"(#!/usr/bin/env bash
if [[ "$1" == "-help" ]]; then
  echo "Usage of casdownloader:" >&2
  echo "  -use-hardlink" >&2  # missing the ending "=...", should never occur.
else
  touch {TARGET_DIR}/artifact-img-name
  rm -rf {CAS_CLIENT_OUTPUT_FILEPATH}
  echo $@ > {CAS_CLIENT_OUTPUT_FILEPATH}
fi
)",
      fmt::arg("TARGET_DIR", target_dir_),
      fmt::arg("CAS_CLIENT_OUTPUT_FILEPATH", cas_output_filepath_));
  downloader_path_ = FakeCasdownloader(script);
  std::unique_ptr<CasDownloader> cas = CasUsingConfig("use-hardlink=false");

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("-use-hardlink=false"));
}

TEST_F(CasDownloaderTests, PassesOptionalStatsFilepathWhenSet) {
  downloader_path_ = FakeDownloaderForArtifactAndFlags(
      target_dir_ + "/artifact-img-name", "dump-json");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  std::string stats_filepath = target_dir_ + "/stats.json";

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      },
      stats_filepath);

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("-dump-json=" + stats_filepath));
}

TEST_F(CasDownloaderTests, RecognizesValidFlags) {
  downloader_path_ = FakeDownloaderForArtifactAndFlags(
      target_dir_ + "/artifact-img-name", "rpc-timeout");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig("rpc-timeout=120s");

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("-cas-instance=cas_instance"));
  EXPECT_THAT(output, HasSubstr("-cas-addr=cas_addr"));
  EXPECT_THAT(output, HasSubstr("-digest=digest"));
  EXPECT_THAT(output, HasSubstr("-dir=" + target_dir_));
  EXPECT_THAT(output, HasSubstr("-rpc-timeout=120s"));
}

TEST_F(CasDownloaderTests, DisablesCacheIfCacheDirNotSet) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("-disable-cache"));
  EXPECT_THAT(output, Not(HasSubstr("-cache-dir=")));
}

TEST_F(CasDownloaderTests, CacheEnabledIfCacheDirSet) {
  downloader_path_ = FakeDownloaderForArtifactAndFlags(
      target_dir_ + "/artifact-img-name", "cache-dir");
  std::unique_ptr<CasDownloader> cas =
      CasUsingConfig("cache-dir=path/to/cache");

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, Not(HasSubstr("-disable-cache")));
  EXPECT_THAT(output, HasSubstr("-cache-dir=path/to/cache"));
}

TEST_F(CasDownloaderTests, FailsIfDigestsFileDoesNotExist) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  bool digest_fetched = false;

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("id_1", "target_1"), "artifact-img-3",
      [&digest_fetched](std::string filename) -> Result<std::string> {
        digest_fetched = true;
        return CF_ERRF("CAS digest for '{}' not found.", filename);
      });

  EXPECT_THAT(download, IsError());
  EXPECT_THAT(FileExists(cas_output_filepath_), IsFalse());
  EXPECT_THAT(digest_fetched, IsTrue());
}

TEST_F(CasDownloaderTests, FailsIfArtifactNotInDigestsFile) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  bool digest_fetched = false;

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("id_1", "target_1"), "artifact-img-3",
      [&digest_fetched, this](std::string filename) -> Result<std::string> {
        digest_fetched = true;
        return CasDigestsFile(filename, "_chunked_artifact-img-1=digest_1",
                              "_chunked_artifact-img-2=digest_2");
      });

  EXPECT_THAT(download, IsError());
  EXPECT_THAT(FileExists(cas_output_filepath_), IsFalse());
  EXPECT_THAT(digest_fetched, IsTrue());
}

TEST_F(CasDownloaderTests, DownloadsDigestsOnlyIfNeeded) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-1");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  bool digest_fetched = false;
  DigestsFn digests_fetcher =
      [&digest_fetched, this](std::string filename) -> Result<std::string> {
    digest_fetched = true;
    return CasDigestsFile(filename, "_chunked_artifact-img-1=digest_1",
                          "_chunked_artifact-img-2=digest_2");
  };

  // Download of the digests always occur for the first artifact.
  Result<std::string> download =
      DownloadWithDigests(*cas, MakeDeviceBuild("id_1", "target_1"),
                          "artifact-img-1", digests_fetcher);

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("digest=digest_1"));
  EXPECT_THAT(digest_fetched, IsTrue());

  // No download of the digests file if for the same build id and target.
  digest_fetched = false;
  EXPECT_THAT(RemoveFileIfExists(cas_output_filepath_), IsOk());
  {
    download = DownloadWithDigests(*cas, MakeDeviceBuild("id_1", "target_1"),
                                   "artifact-img-2", digests_fetcher);
  }
  // Skip checking download result as the fake cas downloader is not
  // sophisticated enough to create the artifacts based on the digests.
  output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("digest=digest_2"));
  EXPECT_THAT(digest_fetched, IsFalse());

  // Downloads the digests file if for a different build id.
  digest_fetched = false;
  EXPECT_THAT(RemoveFileIfExists(cas_output_filepath_), IsOk());
  EXPECT_THAT(RemoveFileIfExists(target_dir_ + "/artifact-img-1"), IsOk());
  {
    download = DownloadWithDigests(*cas, MakeDeviceBuild("id_2", "target_1"),
                                   "artifact-img-1", digests_fetcher);
  }
  output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(download, IsOk());
  EXPECT_THAT(output, HasSubstr("digest=digest_1"));
  EXPECT_THAT(digest_fetched, IsTrue());

  // Downloads the digests file if for a different build target.
  digest_fetched = false;
  EXPECT_THAT(RemoveFileIfExists(cas_output_filepath_), IsOk());
  EXPECT_THAT(RemoveFileIfExists(target_dir_ + "/artifact-img-1"), IsOk());
  {
    download = DownloadWithDigests(*cas, MakeDeviceBuild("id_2", "target_2"),
                                   "artifact-img-1", digests_fetcher);
  }
  EXPECT_THAT(RemoveFileIfExists(target_dir_ + "/artifact-img-1"), IsOk());
  output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("digest=digest_1"));
  EXPECT_THAT(digest_fetched, IsTrue());
}

TEST_F(CasDownloaderTests, DownloadsUncompressedIfPreferUncompressed) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-zip");
  std::unique_ptr<CasDownloader> cas = CasPreferUncompressedUsingConfig();

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("id_1", "target_1"), "artifact-img-zip",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(
            filename, "_chunked_artifact-img-zip=digest_compressed",
            "_chunked_dir_artifact-img-zip=digest_uncompressed");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("digest=digest_uncompressed"));
}

TEST_F(CasDownloaderTests, DownloadsCompressedIfNotPreferUncompressed) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-zip");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("id_1", "target_1"), "artifact-img-zip",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(
            filename, "_chunked_artifact-img-zip=digest_compressed",
            "_chunked_dir_artifact-img-zip=digest_uncompressed");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("digest=digest_compressed"));
}

TEST_F(CasDownloaderTests,
       DownloadsCompressedIfPreferUncompressedButUnavailable) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-zip");
  std::unique_ptr<CasDownloader> cas = CasPreferUncompressedUsingConfig();

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("id_1", "target_1"), "artifact-img-zip",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename,
                              "_chunked_artifact-img-zip=digest_compressed");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("digest=digest_compressed"));
}

TEST_F(CasDownloaderTests, PassesCasOptionsFromCommandLine) {
  downloader_path_ = FakeDownloaderForArtifactAndFlags(
      target_dir_ + "/artifact-img-name", "cache-max-size", "memory-limit",
      "rpc-timeout");
  CasDownloaderFlags flags;
  flags.downloader_path.set_value(downloader_path_);
  flags.cache_max_size.set_value(85899345920LL);  // 80 GiB (int64_t)
  flags.memory_limit.set_value(200);
  flags.rpc_timeout.set_value(120);
  flags.batch_read_blobs_timeout.set_value(120);
  std::string service_account_filepath = "";
  std::unique_ptr<CasDownloader> cas =
      CasFromCommandLine(service_account_filepath, flags);

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  // Verify big numbers match.
  EXPECT_THAT(output, HasSubstr("-cache-max-size=85899345920"));
  EXPECT_THAT(output, HasSubstr("-memory-limit=200"));
  EXPECT_THAT(output, HasSubstr("-rpc-timeout=120s"));
  // Not specified in the command line.
  EXPECT_THAT(output, Not(HasSubstr("-get-tree-timeout=")));
  // Not supported by the fake downloader.
  EXPECT_THAT(output, Not(HasSubstr("-batch-update-blobs-timeout=")));
}

TEST_F(CasDownloaderTests, CacheMaxSize_DefaultWhenAbsent) {
  // Fake downloader advertises support for cache-dir and cache-max-size.
  downloader_path_ = FakeDownloaderForArtifactAndFlags(
      target_dir_ + "/artifact-img-name", "cache-dir", "cache-max-size");
  // Provide cache-dir but omit cache-max-size so default should be used.
  std::unique_ptr<CasDownloader> cas =
      CasUsingConfig("cache-dir=path/to/cache");

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output,
              HasSubstr(fmt::format("-cache-max-size={}", kMinCacheMaxSize)));
}

TEST_F(CasDownloaderTests, CacheMaxSize_OverriddenWhenTooSmall) {
  // Fake downloader advertises support for cache-dir and cache-max-size.
  downloader_path_ = FakeDownloaderForArtifactAndFlags(
      target_dir_ + "/artifact-img-name", "cache-dir", "cache-max-size");
  // Provide a too-small cache-max-size which should be replaced by the default.
  std::unique_ptr<CasDownloader> cas =
      CasUsingConfig("cache-dir=path/to/cache", "cache-max-size=1");

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, Not(HasSubstr("-cache-max-size=1")));
  EXPECT_THAT(output,
              HasSubstr(fmt::format("-cache-max-size={}", kMinCacheMaxSize)));
}

TEST_F(CasDownloaderTests, DownloadFileReturnsTheArtifactPath) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  ASSERT_THAT(cas, Not(Eq(nullptr)));

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOkAndValue(target_dir_ + "/artifact-img-name"));
}

TEST_F(CasDownloaderTests, DownloadsDigestsFromTheDigestsSource) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  ASSERT_THAT(cas, Not(Eq(nullptr)));

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "artifact-img-name",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_artifact-img-name=digest");
      });

  EXPECT_THAT(download, IsOk());
  EXPECT_THAT(digests_source_.requests,
              ElementsAre(target_dir_ + "/cas_digests.json"));
}

TEST_F(CasDownloaderTests, DownloadsUncompressedIntoTheArtifactPath) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-zip");
  std::unique_ptr<CasDownloader> cas = CasPreferUncompressedUsingConfig();
  ASSERT_THAT(cas, Not(Eq(nullptr)));

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("id_1", "target_1"), "artifact-img-zip",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(
            filename, "_chunked_dir_artifact-img-zip=digest_uncompressed");
      });

  EXPECT_THAT(download, IsOkAndValue(target_dir_ + "/artifact-img-zip"));
  std::string output = ReadFile(cas_output_filepath_);
  EXPECT_THAT(output, HasSubstr("-dir=" + target_dir_ + "/artifact-img-zip "));
}

TEST_F(CasDownloaderTests, RejectsArtifactsWithoutImgBeforeAnyWork) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/misc_info.txt");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  ASSERT_THAT(cas, Not(Eq(nullptr)));
  EXPECT_THAT(RemoveFileIfExists(cas_output_filepath_), IsOk());

  Result<std::string> download = DownloadWithDigests(
      *cas, MakeDeviceBuild("build_id", "build_target"), "misc_info.txt",
      [this](std::string filename) -> Result<std::string> {
        return CasDigestsFile(filename, "_chunked_misc_info.txt=digest");
      });

  EXPECT_THAT(download, IsErrorAndMessage(HasSubstr("misc_info.txt")));
  EXPECT_THAT(digests_source_.requests, IsEmpty());
  EXPECT_THAT(FileExists(cas_output_filepath_), IsFalse());
}

TEST_F(CasDownloaderTests, GetBuildIsNotSupported) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  ASSERT_THAT(cas, Not(Eq(nullptr)));

  EXPECT_THAT(cas->GetBuild(DeviceBuildString{.branch_or_id = "build_id",
                                              .target = "build_target"}),
              IsError());
}

TEST_F(CasDownloaderTests, FileReaderIsNotSupported) {
  downloader_path_ =
      FakeDownloaderForArtifactAndFlags(target_dir_ + "/artifact-img-name");
  std::unique_ptr<CasDownloader> cas = CasUsingConfig();
  ASSERT_THAT(cas, Not(Eq(nullptr)));

  EXPECT_THAT(cas->FileReader(MakeDeviceBuild("build_id", "build_target"),
                              "artifact-img-name"),
              IsError());
  EXPECT_THAT(digests_source_.requests, IsEmpty());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

}  // namespace cuttlefish
