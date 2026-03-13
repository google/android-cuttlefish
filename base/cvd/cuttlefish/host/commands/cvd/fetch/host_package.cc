//
// Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/fetch/host_package.h"

#include <android-base/file.h>
#include <sys/stat.h>

#include <optional>
#include <string>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/archive.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_tracer.h"
#include "cuttlefish/host/commands/cvd/fetch/substitute.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_api.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> FetchHostPackage(
    BuildApi& build_api, const Build& build, const std::string& target_dir,
    const bool keep_archives,
    const std::vector<std::string>& host_substitutions,
    FetchTracer::Trace trace) {
  LOG(INFO) << "Preparing host package for " << build;
  // This function is called asynchronously, so it may take a while to start.
  // Complete a phase here to ensure that delay is not counted in the download
  // time.
  // The download time will still include time spent waiting for the mutex in
  // the build_api though.
  trace.CompletePhase("Async start delay");
  auto host_tools_name = GetFilepath(build).value_or("cvd-host_package.tar.gz");
  std::string host_tools_filepath =
      CF_EXPECT(build_api.DownloadFile(build, target_dir, host_tools_name));
  trace.CompletePhase("Download", FileSize(host_tools_filepath));

  CF_EXPECT(
      ExtractArchiveContents(host_tools_filepath, target_dir, keep_archives));
  trace.CompletePhase("Extract");

  CF_EXPECT(HostPackageSubstitution(target_dir, host_substitutions));

  trace.CompletePhase("Substitute");
  return {};
}

}  // namespace cuttlefish
