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

#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fmt/format.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

DEFINE_string(output, "host_bugreport.zip", "Where to write the output");
DEFINE_bool(include_adb_bugreport, false, "Includes device's `adb bugreport`.");

namespace cuttlefish {
namespace {

void SaveFile(Zip& archive, const std::string& zip_path,
              const std::string& file_path) {
  if (!FileExists(file_path)) {
    LOG(ERROR) << "Tried to add file '" << file_path << "', does not exist.";
    return;
  }
  Result<ZipSource> source = ZipSource::FromFile(file_path);
  if (!source.ok()) {
    LOG(ERROR) << "Failed to open '" << file_path << ":\n"
               << source.error().FormatForEnv(/* color = */ false);
    return;
  }
  Result<void> add_res = archive.AddFile(zip_path, std::move(*source));
  if (!add_res.ok()) {
    LOG(ERROR) << "Failed to add '" << file_path << "' to zip at '" << zip_path
               << "': " << add_res.error().FormatForEnv(/* color = */ false);
  }
}

void AddNetsimdLogs(Zip& archive) {
  // The temp directory name depends on whether the `USER` environment variable
  // is defined.
  // https://source.corp.google.com/h/googleplex-android/platform/superproject/main/+/main:tools/netsim/rust/common/src/system/mod.rs;l=37-57;drc=360ddb57df49472a40275b125bb56af2a65395c7
  std::string user = StringFromEnv("USER", "");
  std::string dir = user.empty()
                        ? TempDir() + "/android/netsimd"
                        : fmt::format("{}/android-{}/netsimd", TempDir(), user);
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
    SaveFile(archive, "netsimd/" + name, dir + "/" + name);
  }
}

Result<void> CreateDeviceBugreport(
    const CuttlefishConfig::InstanceSpecific& ins, const std::string& out_dir) {
  std::string adb_bin_path = HostBinaryPath("adb");
  CF_EXPECT(FileExists(adb_bin_path),
            "adb binary not found at: " << adb_bin_path);
  Command connect_cmd("timeout");
  connect_cmd.SetWorkingDirectory(
      "/");  // Use a deterministic working directory
  connect_cmd.AddParameter("30s")
      .AddParameter(adb_bin_path)
      .AddParameter("connect")
      .AddParameter(ins.adb_ip_and_port());
  CF_EXPECT_EQ(connect_cmd.Start().Wait(), 0, "adb connect failed");
  Command wait_for_device_cmd("timeout");
  wait_for_device_cmd.SetWorkingDirectory(
      "/");  // Use a deterministic working directory
  wait_for_device_cmd.AddParameter("30s")
      .AddParameter(adb_bin_path)
      .AddParameter("-s")
      .AddParameter(ins.adb_ip_and_port())
      .AddParameter("wait-for-device");
  CF_EXPECT_EQ(wait_for_device_cmd.Start().Wait(), 0,
               "adb wait-for-device failed");
  Command bugreport_cmd("timeout");
  bugreport_cmd.SetWorkingDirectory(
      "/");  // Use a deterministic working directory
  bugreport_cmd.AddParameter("300s")
      .AddParameter(adb_bin_path)
      .AddParameter("-s")
      .AddParameter(ins.adb_ip_and_port())
      .AddParameter("bugreport")
      .AddParameter(out_dir);
  CF_EXPECT_EQ(bugreport_cmd.Start().Wait(), 0, "adb bugreport failed");
  return {};
}

Result<void> CvdHostBugreportMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  std::string log_filename = TempDir() + "/cvd_hbr.log.XXXXXX";
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

  ZipSource output_file = CF_EXPECT(ZipSource::FromFile(FLAGS_output));

  Zip archive = CF_EXPECT(Zip::CreateFromSource(std::move(output_file)));

  SaveFile(archive, "cuttlefish_assembly/assemble_cvd.log",
           config->AssemblyPath("assemble_cvd.log"));
  SaveFile(archive, "cuttlefish_assembly/cuttlefish_config.json",
           config->AssemblyPath("cuttlefish_config.json"));

  for (const auto& instance : config->Instances()) {
    auto save = [&archive, instance](const std::string& path) {
      const auto& zip_name = instance.instance_name() + "/" + path;
      const auto& file_name = instance.PerInstancePath(path.c_str());
      SaveFile(archive, zip_name, file_name);
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

    if (FLAGS_include_adb_bugreport) {
      // TODO(b/359657254) Create the `adb bugreport` asynchronously.
      std::string device_br_dir = TempDir() + "/cvd_dbrXXXXXX";
      CF_EXPECTF(mkdtemp(device_br_dir.data()) != nullptr,
                 "mkdtemp failed: '{}'", strerror(errno));
      auto result = CreateDeviceBugreport(instance, device_br_dir);
      if (result.ok()) {
        auto names = DirectoryContents(device_br_dir);
        if (names.ok()) {
          for (const auto& name : names.value()) {
            std::string filename = device_br_dir + "/" + name;
            SaveFile(archive, android::base::Basename(filename), filename);
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

  AddNetsimdLogs(archive);

  LOG(INFO) << "Building cvd bugreport completed";

  SaveFile(archive, "cvd_bugreport_builder.log", log_filename);

  Result<void> finalized = Zip::Finalize(std::move(archive));
  if (finalized.ok()) {
    LOG(INFO) << "Saved to \"" << FLAGS_output << "\"";
  } else {
    LOG(ERROR) << "Error in writing '" << FLAGS_output << "': \n"
               << finalized.error().FormatForEnv();
  }

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
