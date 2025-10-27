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

#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fmt/format.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/zip/zip_file.h"

DEFINE_string(output, "host_bugreport.zip", "Where to write the output");
DEFINE_bool(include_adb_bugreport, false, "Includes device's `adb bugreport`.");

namespace cuttlefish {
namespace {

template <typename T>
void LogError(Result<T> res) {
  if (!res.ok()) {
    LOG(ERROR) << res.error().FormatForEnv(/* color = */ false);
  }
}

Result<void> AddNetsimdLogs(WritableZip& archive) {
  // The temp directory name depends on whether the `USER` environment variable
  // is defined.
  // https://source.corp.google.com/h/googleplex-android/platform/superproject/main/+/main:tools/netsim/rust/common/src/system/mod.rs;l=37-57;drc=360ddb57df49472a40275b125bb56af2a65395c7
  std::string user = StringFromEnv("USER", "");
  std::string dir = user.empty()
                        ? TempDir() + "/android/netsimd"
                        : fmt::format("{}/android-{}/netsimd", TempDir(), user);
  CF_EXPECTF(DirectoryExists(dir),
             "netsimd logs directory: `{}` does not exist.", dir);
  auto names = CF_EXPECTF(DirectoryContents(dir),
                          "Cannot read from netsimd directory `{}`", dir);
  for (const auto& name : names) {
    LogError(AddFileAt(archive, dir + "/" + name, "netsimd/" + name));
  }
  return {};
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

Result<void> AddAdbBugreport(const CuttlefishConfig::InstanceSpecific& instance,
                             WritableZip& archive) {
  // TODO(b/359657254) Create the `adb bugreport` asynchronously.
  std::string device_br_dir = TempDir() + "/cvd_dbrXXXXXX";
  CF_EXPECTF(mkdtemp(device_br_dir.data()) != nullptr, "mkdtemp failed: '{}'",
             StrError(errno));
  CF_EXPECT(CreateDeviceBugreport(instance, device_br_dir),
            "Failed to create device bugreport");
  auto names = CF_EXPECT(DirectoryContents(device_br_dir),
                         "Cannot read from device bugreport directory");
  for (const auto& name : names) {
    std::string filename = device_br_dir + "/" + name;
    LogError(AddFileAt(archive, filename, android::base::Basename(filename)));
  }
  static_cast<void>(RecursivelyRemoveDirectory(device_br_dir));
  return {};
}

// This function will gather as much as it can. It logs any errors it runs into,
// but doesn't propagate them because a partial bug report is still useful and
// the fact that something was missing/inaccessible is still useful debugging
// information.
void TakeHostBugreport(const CuttlefishConfig* config, WritableZip& archive) {
  LogError(AddFileAt(archive, config->AssemblyPath("assemble_cvd.log"),
                     "cuttlefish_assembly/assemble_cvd.log"));
  LogError(AddFileAt(archive, config->AssemblyPath("cuttlefish_config.json"),
                     "cuttlefish_assembly/cuttlefish_config.json"));

  for (const auto& instance : config->Instances()) {
    auto save = [&archive, instance](const std::string& path) {
      const auto& zip_name = instance.instance_name() + "/" + path;
      const auto& file_name = instance.PerInstancePath(path.c_str());
      LogError(AddFileAt(archive, file_name, zip_name));
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
      LogError(AddAdbBugreport(instance, archive));
    }
  }

  LogError(AddNetsimdLogs(archive));

  LOG(INFO) << "Building cvd bugreport completed";
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

  WritableZip archive = CF_EXPECT(ZipOpenReadWrite(FLAGS_output));

  // Only logs errors, but doesn't return them.
  TakeHostBugreport(config, archive);

  LogError(AddFileAt(archive, log_filename, "cvd_bugreport_builder.log"));

  LogError(WritableZip::Finalize(std::move(archive)));

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
