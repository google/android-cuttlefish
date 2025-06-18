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

#include "cuttlefish/host/libs/zip/zip_builder.h"

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {

ZipBuilder::ZipBuilder(Zip archive) : archive_(std::move(archive)) {}

Result<ZipBuilder> ZipBuilder::AppendingTo(Zip existing) {
  return ZipBuilder(std::move(existing));
}

Result<ZipBuilder> ZipBuilder::TargetingFile(const std::string& fs_path) {
  ZipSource source = CF_EXPECT(ZipSource::FromFile(fs_path));
  return CF_EXPECT(ZipBuilder::TargetingSource(std::move(source)));
}

Result<ZipBuilder> ZipBuilder::TargetingSource(ZipSource source) {
  Zip zip = CF_EXPECT(Zip::CreateFromSource(std::move(source)));
  return CF_EXPECT(ZipBuilder::AppendingTo(std::move(zip)));
}

Result<void> ZipBuilder::AddFile(const std::string& fs_path) {
  CF_EXPECT(AddFileAt(fs_path, fs_path));
  return {};
}

Result<void> ZipBuilder::AddFileAt(const std::string& fs_path,
                                   const std::string& zip_path) {
  CF_EXPECTF(FileExists(fs_path), "No file in the filesystem at '{}'", fs_path);
  ZipSource source = CF_EXPECT(ZipSource::FromFile(fs_path));
  CF_EXPECT(AddDataAt(std::move(source), zip_path));
  return {};
}

Result<void> ZipBuilder::AddDataAt(const std::string& data,
                                   const std::string& zip_path) {
  CF_EXPECT(AddDataAt(data.data(), data.size(), zip_path));
  return {};
}

Result<void> ZipBuilder::AddDataAt(const std::vector<char>& data,
                                   const std::string& zip_path) {
  CF_EXPECT(AddDataAt(data.data(), data.size(), zip_path));
  return {};
}

Result<void> ZipBuilder::AddDataAt(const void* data, size_t size,
                                   const std::string& zip_path) {
  ZipSource source = CF_EXPECT(ZipSource::FromData(data, size));
  CF_EXPECT(AddDataAt(std::move(source), zip_path));
  return {};
}

Result<void> ZipBuilder::AddDataAt(ZipSource source,
                                   const std::string& zip_path) {
  CF_EXPECT(archive_.AddFile(zip_path, std::move(source)));
  return {};
}

Zip ZipBuilder::ToRaw(ZipBuilder builder) {
  return std::move(builder.archive_);
}

Result<void> ZipBuilder::Finalize(ZipBuilder builder) {
  CF_EXPECT(Zip::Finalize(std::move(builder.archive_)));
  return {};
}

}  // namespace cuttlefish
