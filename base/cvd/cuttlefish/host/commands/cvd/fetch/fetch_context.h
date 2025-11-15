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

#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/fetch/builds.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_tracer.h"
#include "cuttlefish/host/commands/cvd/fetch/target_directories.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"

namespace cuttlefish {

class FetchArtifact {
 public:
  Result<void> Download();
  Result<void> DownloadTo(std::string local_path);

  Result<ReadableZip*> AsZip();

  Result<void> ExtractAll();
  Result<void> ExtractAll(const std::string& local_path);

  Result<void> ExtractOne(const std::string& member_name);
  Result<void> ExtractOneTo(const std::string& member_name,
                            const std::string& local_path);

  Result<void> DeleteLocalFile();

 private:
  friend class FetchBuildContext;

  FetchArtifact(class FetchBuildContext&, std::string artifact_name);

  FetchBuildContext& fetch_build_context_;
  std::string artifact_name_;
  std::string downloaded_path_;
  std::optional<ReadableZip> zip_;
};

/**
 * Wraps standard download operations with cross-cutting concerns:
 * - Tracing long-running operations with time used.
 * - Tracking the source build of created files.
 * - Placing files under the right target directory.
 * - Desparsing images.
 *
 * File paths for return values and argument values are relative to the target
 * directory.
 *
 * By hiding the target directory from direct access, IO operations are funneled
 * through an instance of this type, which guarantees none of the cross-cutting
 * concerns are missed. Additionally, this could be replaced with a fake
 * implementation later to support unit testing the business logic.
 */
class FetchBuildContext {
 public:
  const Build& Build() const;
  std::string GetBuildZipName(const std::string&) const;
  // The specific filepath the user requested for a particular build. Ignored
  // for some builds.
  std::optional<std::string> GetFilepath() const;

  FetchArtifact Artifact(std::string artifact_name);

 private:
  friend class FetchArtifact;
  friend class FetchContext;

  FetchBuildContext(class FetchContext&, ::cuttlefish::Build,
                    std::string_view target_directory, FileSource,
                    FetchTracer::Trace);

  Result<void> DesparseFiles(std::vector<std::string> files);

  Result<void> AddFileToConfig(std::string);

  class FetchContext& fetch_context_;
  ::cuttlefish::Build build_;
  std::string target_directory_;
  FileSource file_source_;
  FetchTracer::Trace trace_;
};

std::ostream& operator<<(std::ostream&, const FetchBuildContext&);

/**
 * References common state used by most download operations and produces
 * `FetchBuildContext` instances.
 */
class FetchContext {
 public:
  FetchContext(BuildApi&, const TargetDirectories&, const Builds&,
               FetcherConfig&, FetchTracer&);

  std::optional<FetchBuildContext> DefaultBuild();
  std::optional<FetchBuildContext> SystemBuild();
  std::optional<FetchBuildContext> KernelBuild();
  std::optional<FetchBuildContext> BootBuild();
  std::optional<FetchBuildContext> BootloaderBuild();
  std::optional<FetchBuildContext> AndroidEfiLoaderBuild();
  std::optional<FetchBuildContext> OtaToolsBuild();

 private:
  friend class FetchArtifact;
  friend class FetchBuildContext;

  BuildApi& build_api_;
  const TargetDirectories& target_directories_;
  const Builds& builds_;
  FetcherConfig& fetcher_config_;
  FetchTracer& tracer_;
};

}  // namespace cuttlefish
