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

#include "cuttlefish/host/libs/web/build_api_zip.h"

#include <string>
#include <utility>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/buffered_zip_source.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {

Result<ReadableZip> OpenZip(BuildApi& build_api, const Build& build,
                            const std::string& name) {
  SeekableZipSource source = CF_EXPECT(build_api.FileReader(build, name));

  SeekableZipSource buffered =
      CF_EXPECT(BufferZipSource(std::move(source), 1 << 16));

  return CF_EXPECT(ReadableZip::FromSource(std::move(buffered)));
}

}  // namespace cuttlefish
