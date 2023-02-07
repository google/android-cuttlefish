/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <stdio.h>
#include <fstream>
#include <string>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "ziparchive/zip_writer.h"

DEFINE_string(output, "host_bugreport.zip", "Where to write the output");

namespace cuttlefish {
namespace {

void SaveFile(ZipWriter& writer, const std::string& zip_path,
              const std::string& file_path) {
  writer.StartEntry(zip_path, ZipWriter::kCompress | ZipWriter::kAlign32);
  std::fstream file(file_path, std::fstream::in | std::fstream::binary);
  do {
    char data[1024 * 10];
    file.read(data, sizeof(data));
    writer.WriteBytes(data, file.gcount());
  } while (file);
  writer.FinishEntry();
  if (file.bad()) {
    LOG(ERROR) << "Error in logging " << file_path << " to " << zip_path;
  }
}

Result<void> CvdHostBugreportMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = CuttlefishConfig::Get();
  CHECK(config) << "Unable to find the config";

  auto out_path = FLAGS_output.c_str();
  std::unique_ptr<FILE, decltype(&fclose)> out(fopen(out_path, "wbe"), &fclose);
  ZipWriter writer(out.get());

  auto save = [&writer, config](const std::string& path) {
    SaveFile(writer, "cuttlefish_assembly/" + path, config->AssemblyPath(path));
  };
  save("assemble_cvd.log");
  save("cuttlefish_config.json");

  for (const auto& instance : config->Instances()) {
    auto save = [&writer, instance](const std::string& path) {
      const auto& zip_name = instance.instance_name() + "/" + path;
      const auto& file_name = instance.PerInstancePath(path.c_str());
      SaveFile(writer, zip_name, file_name);
    };
    save("cuttlefish_config.json");
    save("disk_config.txt");
    save("kernel.log");
    save("launcher.log");
    save("logcat");
    save("metrics.log");
    auto tombstones =
        CF_EXPECT(DirectoryContents(instance.PerInstancePath("tombstones")),
                  "Cannot read from tombstones directory.");
    for (const auto& tombstone : tombstones) {
      if (tombstone == "." || tombstone == "..") {
        continue;
      }
      save("tombstones/" + tombstone);
    }
    auto recordings =
        CF_EXPECT(DirectoryContents(instance.PerInstancePath("recording")),
                  "Cannot read from recording directory.");
    for (const auto& recording : recordings) {
      if (recording == "." || recording == "..") {
        continue;
      }
      save("recording/" + recording);
    }
  }

  writer.Finish();

  LOG(INFO) << "Saved to \"" << FLAGS_output << "\"";

  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto result = cuttlefish::CvdHostBugreportMain(argc, argv);
  CHECK(result.ok()) << result.error().Message();
  return 0;
}
