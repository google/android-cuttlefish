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

#include "super_image_mixer.h"

#include <cstdio>
#include <functional>
#include <memory>

#include <glog/logging.h>

#include "ziparchive/zip_archive.h"
#include "ziparchive/zip_writer.h"

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace {

using cvd::FileExists;
using vsoc::DefaultHostArtifactsPath;

std::string TargetFilesZip(const cvd::FetcherConfig& fetcher_config,
                           cvd::FileSource source) {
  for (const auto& file_iter : fetcher_config.get_cvd_files()) {
    if (file_iter.second.source != source) {
      continue;
    }
    std::string expected_substr = "target_files-" + file_iter.second.build_id + ".zip";
    auto expected_pos = file_iter.first.size() - expected_substr.size();
    if (file_iter.first.rfind(expected_substr) == expected_pos) {
      return file_iter.first;
    }
  }
  return "";
}

class ZipArchiveDeleter {
public:
  void operator()(ZipArchive* archive) {
    CloseArchive(archive);
  }
};

using ManagedZipArchive = std::unique_ptr<ZipArchive, ZipArchiveDeleter>;

class CFileCloser {
public:
  void operator()(FILE* file) {
    fclose(file);
  }
};

using ManagedCFile = std::unique_ptr<FILE, CFileCloser>;

ManagedZipArchive OpenZipArchive(const std::string& path) {
  ZipArchive* ptr;
  int status = OpenArchive(path.c_str(), &ptr);
  if (status != 0) {
    LOG(ERROR) << "Could not open archive \"" << path << "\": " << status;
    return {};
  }
  return ManagedZipArchive(ptr);
}

ManagedCFile OpenFile(const std::string& path, const std::string& mode) {
  FILE* ptr = fopen(path.c_str(), mode.c_str());
  if (ptr == nullptr) {
    int error_num = errno;
    LOG(ERROR) << "Could not open \"" << path << "\". Error was "
               << strerror(error_num);
    return {};
  }
  return ManagedCFile(ptr);
}

const std::string kMiscInfoPath = "META/misc_info.txt";
const std::string kSystemPath = "IMAGES/system.img";
const std::string kSystemExtPath = "IMAGES/system_ext.img";

bool CopyZipFileContents(const uint8_t* buf, size_t buf_size, void* cookie) {
  ZipWriter* out_writer = (ZipWriter*) cookie;
  int32_t status = out_writer->WriteBytes(buf, buf_size);
  if (status != 0) {
    LOG(ERROR) << "Could not write zip file contents, error code " << status;
    return false;
  }
  return true;
}

bool CombineTargetZipFiles(const std::string& default_target_zip,
                           const std::string& system_target_zip,
                           const std::string& output_path) {
  auto default_target = OpenZipArchive(default_target_zip);
  if (!default_target) {
    LOG(ERROR) << "Could not open " << default_target_zip;
    return false;
  }
  auto system_target = OpenZipArchive(system_target_zip);
  if (!system_target) {
    LOG(ERROR) << "Could not open " << system_target_zip;
    return false;
  }
  auto out_file = OpenFile(output_path, "wb");
  if (!out_file) {
    LOG(ERROR) << "Could not open " << output_path;
    return false;
  }
  ZipWriter out_writer{out_file.get()};

  ZipEntry entry;
  if (FindEntry(system_target.get(), kSystemPath, &entry) != 0) {
    LOG(ERROR) << "System target files zip does not have " << kSystemPath;
    return false;
  }

  if (FindEntry(default_target.get(), kMiscInfoPath, &entry) != 0) {
    LOG(ERROR) << "Default target files zip does not have " << kMiscInfoPath;
    return false;
  }
  out_writer.StartEntry(kMiscInfoPath, 0);
  ProcessZipEntryContents(
      default_target.get(), &entry, CopyZipFileContents, (void*) &out_writer);
  out_writer.FinishEntry();

  bool system_target_has_ext =
      FindEntry(system_target.get(), kSystemExtPath, &entry) == 0;

  void* iteration_cookie;
  std::string name;

  StartIteration(default_target.get(), &iteration_cookie, "IMAGES/", ".img");
  for (int status = 0; status != -1; status = Next(iteration_cookie, &entry, &name)) {
    if (name == "") {
      continue;
    }
    LOG(INFO) << "Name is \"" << name << "\"";
    if (name == kSystemPath) {
      continue;
    } else if (system_target_has_ext && name == kSystemExtPath) {
      continue;
    }
    LOG(INFO) << "Writing " << name;
    out_writer.StartEntry(name, 0);
    ProcessZipEntryContents(
        default_target.get(), &entry, CopyZipFileContents, (void*) &out_writer);
    out_writer.FinishEntry();
  }
  EndIteration(iteration_cookie);

  StartIteration(system_target.get(), &iteration_cookie, "IMAGES/", ".img");
  for (int status = 0; status != -1; status = Next(iteration_cookie, &entry, &name)) {
    bool is_system_image = name == kSystemPath;
    bool is_system_ext_image = name == kSystemExtPath;
    if (!is_system_image && !is_system_ext_image) {
      continue;
    }
    LOG(INFO) << "Writing " << name;
    out_writer.StartEntry(name, 0);
    ProcessZipEntryContents(
        system_target.get(), &entry, CopyZipFileContents, (void*) &out_writer);
    out_writer.FinishEntry();
  }
  EndIteration(iteration_cookie);

  int success = out_writer.Finish();
  if (success != 0) {
    LOG(ERROR) << "Unable to write combined image zip archive: " << success;
    return false;
  }

  return true;
}

bool BuildSuperImage(const std::string& combined_target_zip,
                     const std::string& output_path) {
  std::string build_super_image_binary;
  std::string otatools_path;
  if (FileExists(DefaultHostArtifactsPath("otatools/bin/build_super_image"))) {
    build_super_image_binary =
        DefaultHostArtifactsPath("otatools/bin/build_super_image");
    otatools_path = DefaultHostArtifactsPath("otatools");
  } else if (FileExists(DefaultHostArtifactsPath("bin/build_super_image"))) {
    build_super_image_binary =
        DefaultHostArtifactsPath("bin/build_super_image");
    otatools_path = DefaultHostArtifactsPath("");
  } else {
    LOG(ERROR) << "Could not find otatools";
    return false;
  }
  return cvd::execute({
    build_super_image_binary,
    "--path=" + otatools_path,
    combined_target_zip,
    output_path,
  }) == 0;
}

} // namespace

bool SuperImageNeedsRebuilding(const cvd::FetcherConfig& fetcher_config,
                               const vsoc::CuttlefishConfig&) {
  bool has_default_build = false;
  bool has_system_build = false;
  for (const auto& file_iter : fetcher_config.get_cvd_files()) {
    if (file_iter.second.source == cvd::FileSource::DEFAULT_BUILD) {
      has_default_build = true;
    } else if (file_iter.second.source == cvd::FileSource::SYSTEM_BUILD) {
      has_system_build = true;
    }
  }
  return has_default_build && has_system_build;
}

bool RebuildSuperImage(const cvd::FetcherConfig& fetcher_config,
                       const vsoc::CuttlefishConfig& config,
                       const std::string& output_path) {
  std::string default_target_zip =
      TargetFilesZip(fetcher_config, cvd::FileSource::DEFAULT_BUILD);
  if (default_target_zip == "") {
    LOG(ERROR) << "Unable to find default target zip file.";
    return false;
  }
  std::string system_target_zip =
      TargetFilesZip(fetcher_config, cvd::FileSource::SYSTEM_BUILD);
  if (system_target_zip == "") {
    LOG(ERROR) << "Unable to find system target zip file.";
    return false;
  }
  std::string combined_target_zip = config.PerInstancePath("target_combined.zip");
  // TODO(schuffelen): Use otatools/bin/merge_target_files
  if (!CombineTargetZipFiles(default_target_zip, system_target_zip,
                             combined_target_zip)) {
    LOG(ERROR) << "Could not combine target zip files.";
    return false;
  }
  bool success = BuildSuperImage(combined_target_zip, output_path);
  if (!success) {
    LOG(ERROR) << "Could not write the final output super image.";
  }
  return success;
}
