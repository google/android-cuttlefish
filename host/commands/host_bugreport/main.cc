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

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/tee_logging.h"
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

void AddNetsimdLogs(ZipWriter& writer) {
  // The temp directory name depends on whether the `USER` environment variable
  // is defined.
  // https://source.corp.google.com/h/googleplex-android/platform/superproject/main/+/main:tools/netsim/rust/common/src/system/mod.rs;l=37-57;drc=360ddb57df49472a40275b125bb56af2a65395c7
  std::string user = StringFromEnv("USER", "");
  std::string dir = user.empty() ? "/tmp/android/netsimd"
                                 : fmt::format("/tmp/android-{}/netsimd", user);
  if (!DirectoryExists(dir)) {
    LOG(INFO) << "netsimd logs directory: `" << dir << "` does not exist.";
    return;
  }
  auto names = DirectoryContents(dir);
  if (!names.ok()) {
    LOG(ERROR) << "Cannot read from netsimd directory `" << dir
               << "`: " << names.error().FormatForEnv(/* color = */ false);
    return;
  }
  for (const auto& name : names.value()) {
    SaveFile(writer, "netsimd/" + name, dir + "/" + name);
  }
}

Result<void> CreateDeviceBugreport(
    const CuttlefishConfig::InstanceSpecific& ins, const std::string& out_dir) {
  Command adb_command(HostBinaryPath("adb"));
  adb_command.SetWorkingDirectory(
      "/");  // Use a deterministic working directory
  adb_command.AddParameter("-s").AddParameter(ins.adb_ip_and_port());
  adb_command.AddParameter("wait-for-device");
  adb_command.AddParameter("bugreport");
  adb_command.AddParameter(out_dir);
  CF_EXPECT_EQ(adb_command.Start().Wait(), 0);
  return {};
}

Result<void> CvdHostBugreportMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  std::string log_filename = "/tmp/cvd_hbr.log.XXXXXX";
  {
    auto fd = SharedFD::Mkstemp(&log_filename);
    CF_EXPECT(fd->IsOpen(), "Unable to create log file: " << fd->StrError());
    android::base::SetLogger(TeeLogger({
        {ConsoleSeverity(), SharedFD::Dup(2), MetadataLevel::ONLY_MESSAGE},
        {LogFileSeverity(), fd, MetadataLevel::FULL},
    }));
  }

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
    if (DirectoryExists(instance.PerInstancePath("logs"))) {
      auto result = DirectoryContents(instance.PerInstancePath("logs"));
      if (result.ok()) {
        for (const auto& log : result.value()) {
          save("logs/" + log);
        }
      } else {
        LOG(ERROR) << "Cannot read from logs directory: "
                   << result.error().FormatForEnv(/* color = */ false);
      }
    } else {
      save("kernel.log");
      save("launcher.log");
      save("logcat");
      save("metrics.log");
    }

    {
      auto result = DirectoryContents(instance.PerInstancePath("tombstones"));
      if (result.ok()) {
        for (const auto& tombstone : result.value()) {
          save("tombstones/" + tombstone);
        }
      } else {
        LOG(ERROR) << "Cannot read from tombstones directory: "
                   << result.error().FormatForEnv(/* color = */ false);
      }
    }

    {
      auto result = DirectoryContents(instance.PerInstancePath("recording"));
      if (result.ok()) {
        for (const auto& recording : result.value()) {
          save("recording/" + recording);
        }
      } else {
        LOG(ERROR) << "Cannot read from recording directory: "
                   << result.error().FormatForEnv(/* color = */ false);
      }
    }

    {
      // TODO(b/359657254) Create the `adb bugreport` asynchronously.
      std::string device_br_dir = "/tmp/cvd_dbrXXXXXX";
      CF_EXPECTF(mkdtemp(device_br_dir.data()) != nullptr,
                 "mkdtemp failed: '{}'", strerror(errno));
      auto result = CreateDeviceBugreport(instance, device_br_dir);
      if (result.ok()) {
        auto names = DirectoryContents(device_br_dir);
        if (names.ok()) {
          for (const auto& name : names.value()) {
            std::string filename = device_br_dir + "/" + name;
            SaveFile(writer, cpp_basename(filename), filename);
          }
        } else {
          LOG(ERROR) << "Cannot read from device bugreport directory: "
                     << names.error().FormatForEnv(/* color = */ false);
        }
      } else {
        LOG(ERROR) << "Failed to create device bugreport: "
                   << result.error().FormatForEnv(/* color = */ false);
      }
      static_cast<void>(RecursivelyRemoveDirectory(device_br_dir));
    }
  }

  AddNetsimdLogs(writer);

  SaveFile(writer, "cvd_host_bugreport.log", log_filename);

  writer.Finish();

  LOG(INFO) << "Saved to \"" << FLAGS_output << "\"";

  if (!RemoveFile(log_filename)) {
    LOG(INFO) << "Failed to remove host bug report log file: " << log_filename;
  }

  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto result = cuttlefish::CvdHostBugreportMain(argc, argv);
  CHECK(result.ok()) << result.error().FormatForEnv();
  return 0;
}
