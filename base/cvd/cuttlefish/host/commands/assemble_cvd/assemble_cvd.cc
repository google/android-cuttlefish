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

#include <iostream>
#include <string_view>

#include <android-base/logging.h>
#include <android-base/parsebool.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/posix/symlink.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/clean.h"
#include "cuttlefish/host/commands/assemble_cvd/create_dynamic_disk_files.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/ap_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/factory_reset_protected.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/metadata_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/os_composite_disk.h"
#include "cuttlefish/host/commands/assemble_cvd/display.h"
#include "cuttlefish/host/commands/assemble_cvd/flag_feature.h"
#include "cuttlefish/host/commands/assemble_cvd/flags.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/initramfs_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/super_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vendor_boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/commands/assemble_cvd/resolve_instance_files.h"
#include "cuttlefish/host/commands/assemble_cvd/touchpad.h"
#include "cuttlefish/host/libs/command_util/snapshot_utils.h"
#include "cuttlefish/host/libs/config/adb/adb.h"
#include "cuttlefish/host/libs/config/config_flag.h"
#include "cuttlefish/host/libs/config/custom_actions.h"
#include "cuttlefish/host/libs/config/defaults/defaults.h"
#include "cuttlefish/host/libs/config/fastboot/fastboot.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/feature/inject.h"

namespace cuttlefish {
namespace {

static constexpr std::string_view kFetcherConfigFile = "fetcher_config.json";

FetcherConfigs FindFetcherConfigs(
    const SystemImageDirFlag& system_image_dir) {
  std::vector<FetcherConfig> fetcher_configs;
  for (size_t i = 0; i < system_image_dir.Size(); ++i) {
    std::string fetcher_file =
        fmt::format("{}/{}", system_image_dir.ForIndex(i), kFetcherConfigFile);
    FetcherConfig fetcher_config;
    if (!fetcher_config.LoadFromFile(fetcher_file)) {
      LOG(DEBUG) << "No valid fetcher_config in '" << fetcher_file
                 << "', falling back to default";
    }
    fetcher_configs.emplace_back(std::move(fetcher_config));
  }
  return FetcherConfigs::Create(std::move(fetcher_configs));
}

std::string GetLegacyConfigFilePath(const CuttlefishConfig& config) {
  return config.ForDefaultInstance().PerInstancePath("cuttlefish_config.json");
}

Result<void> SaveConfig(const CuttlefishConfig& tmp_config_obj) {
  auto config_file = GetConfigFilePath(tmp_config_obj);
  auto config_link = GetGlobalConfigFileLink();
  // Save the config object before starting any host process
  CF_EXPECT(tmp_config_obj.SaveToFile(config_file),
            "Failed to save to \"" << config_file << "\"");
  auto legacy_config_file = GetLegacyConfigFilePath(tmp_config_obj);
  CF_EXPECT(tmp_config_obj.SaveToFile(legacy_config_file),
            "Failed to save to \"" << legacy_config_file << "\"");

  setenv(kCuttlefishConfigEnvVarName, config_file.c_str(), true);
  // TODO(schuffelen): Find alternative for host-sandboxing mode
  if (!InSandbox()) {
    CF_EXPECT(Symlink(config_file, config_link));
  }

  return {};
}

#ifndef O_TMPFILE
# define O_TMPFILE (020000000 | O_DIRECTORY)
#endif

Result<void> CreateLegacySymlinks(
    const CuttlefishConfig::InstanceSpecific& instance,
    const CuttlefishConfig::EnvironmentSpecific& environment) {
  std::string log_files[] = {"kernel.log",
                             "launcher.log",
                             "logcat",
                             "metrics.log",
                             "modem_simulator.log",
                             "crosvm_openwrt.log",
                             "crosvm_openwrt_boot.log"};
  for (const auto& log_file : log_files) {
    auto symlink_location = instance.PerInstancePath(log_file.c_str());
    auto log_target = "logs/" + log_file;  // Relative path
    if (FileExists(symlink_location, /* follow_symlinks */ false)) {
      CF_EXPECT(RemoveFile(symlink_location),
                "Failed to remove symlink " << symlink_location);
    }
    CF_EXPECT(Symlink(log_target, symlink_location));
  }

  std::stringstream legacy_instance_path_stream;
  legacy_instance_path_stream << FLAGS_instance_dir;
  if (gflags::GetCommandLineFlagInfoOrDie("instance_dir").is_default) {
    legacy_instance_path_stream << "_runtime";
  }
  legacy_instance_path_stream << "." << instance.id();
  auto legacy_instance_path = legacy_instance_path_stream.str();

  if (DirectoryExists(legacy_instance_path, /* follow_symlinks */ false)) {
    CF_EXPECT(RecursivelyRemoveDirectory(legacy_instance_path));
  } else if (FileExists(legacy_instance_path, /* follow_symlinks */ false)) {
    CF_EXPECT(RemoveFile(legacy_instance_path),
              "Failed to remove instance_dir symlink " << legacy_instance_path);
  }
  // TODO(schuffelen): Find alternative for host-sandboxing mode
  if (!InSandbox()) {
    CF_EXPECT(Symlink(instance.instance_dir(), legacy_instance_path));
  }

  const auto mac80211_uds_name = "vhost_user_mac80211";

  const auto mac80211_uds_path =
      environment.PerEnvironmentUdsPath(mac80211_uds_name);
  const auto legacy_mac80211_uds_path =
      instance.PerInstanceInternalPath(mac80211_uds_name);

  CF_EXPECT(Symlink(mac80211_uds_path, legacy_mac80211_uds_path));

  return {};
}

Result<void> RestoreHostFiles(const std::string& cuttlefish_root_dir,
                              const std::string& snapshot_dir_path) {
  const auto meta_json_path = SnapshotMetaJsonPath(snapshot_dir_path);

  auto guest_snapshot_dirs =
      CF_EXPECT(GuestSnapshotDirectories(snapshot_dir_path));
  auto filter_guest_dir =
      [&guest_snapshot_dirs](const std::string& src_dir) -> bool {
    if (android::base::EndsWith(src_dir, "logs") &&
        Contains(guest_snapshot_dirs, src_dir)) {
      return false;
    }
    return !Contains(guest_snapshot_dirs, src_dir);
  };
  // cp -r snapshot_dir_path HOME
  CF_EXPECT(CopyDirectoryRecursively(snapshot_dir_path, cuttlefish_root_dir,
                                     /* delete destination first */ false,
                                     filter_guest_dir));

  return {};
}

Result<std::set<std::string>> PreservingOnResume(
    const bool creating_os_disk, const int modem_simulator_count) {
  const auto snapshot_path = FLAGS_snapshot_path;
  const bool resume_requested = FLAGS_resume || !snapshot_path.empty();
  if (!resume_requested) {
    if (InSandbox()) {
      return {{"launcher.log"}};
    } else {
      return {};
    }
  }
  CF_EXPECT(snapshot_path.empty() || !creating_os_disk,
            "Restoring from snapshot requires not creating OS disks");
  if (creating_os_disk) {
    // not snapshot restore, must be --resume
    LOG(INFO) << "Requested resuming a previous session (the default behavior) "
              << "but the base images have changed under the overlay, making "
              << "the overlay incompatible. Wiping the overlay files.";
    if (InSandbox()) {
      return {{"launcher.log"}};
    } else {
      return {};
    }
  }

  // either --resume && !creating_os_disk, or restoring from a snapshot
  std::set<std::string> preserving;
  preserving.insert("overlay.img");
  preserving.insert("ap_composite.img");
  preserving.insert("ap_composite_disk_config.txt");
  preserving.insert("ap_composite_gpt_footer.img");
  preserving.insert("ap_composite_gpt_header.img");
  preserving.insert("ap_overlay.img");
  preserving.insert("os_composite_disk_config.txt");
  preserving.insert("os_composite_gpt_header.img");
  preserving.insert("os_composite_gpt_footer.img");
  preserving.insert("os_composite.img");
  preserving.insert("os_vbmeta.img");
  preserving.insert("sdcard.img");
  preserving.insert("sdcard_overlay.img");
  preserving.insert("boot_repacked.img");
  preserving.insert("vendor_dlkm_repacked.img");
  preserving.insert("vendor_boot_repacked.img");
  preserving.insert("access-kregistry");
  preserving.insert("hwcomposer-pmem");
  preserving.insert("NVChip");
  preserving.insert("gatekeeper_secure");
  preserving.insert("gatekeeper_insecure");
  preserving.insert("keymint_secure_deletion_data");
  preserving.insert("modem_nvram.json");
  preserving.insert("recording");
  preserving.insert("persistent_composite_disk_config.txt");
  preserving.insert("persistent_composite_gpt_header.img");
  preserving.insert("persistent_composite_gpt_footer.img");
  preserving.insert("persistent_composite.img");
  preserving.insert("persistent_composite_overlay.img");
  preserving.insert("pflash.img");
  preserving.insert("uboot_env.img");
  preserving.insert(FactoryResetProtectedImage::FileName());
  preserving.insert(MiscImage::Name());
  preserving.insert("vmmtruststore.img");
  preserving.insert(MetadataImage::Name());
  preserving.insert("persistent_vbmeta.img");
  preserving.insert("oemlock_secure");
  preserving.insert("oemlock_insecure");
  // Preserve logs if restoring from a snapshot.
  if (!snapshot_path.empty()) {
    preserving.insert("kernel.log");
    preserving.insert("launcher.log");
    preserving.insert("logcat");
    preserving.insert("modem_simulator.log");
    preserving.insert("crosvm_openwrt.log");
    preserving.insert("crosvm_openwrt_boot.log");
    preserving.insert("metrics.log");
    preserving.insert("userdata.img");
  }
  if (InSandbox()) {
    preserving.insert("launcher.log");  // Created before `assemble_cvd` runs
  }
  for (int i = 0; i < modem_simulator_count; i++) {
    std::stringstream ss;
    ss << "iccprofile_for_sim" << i << ".xml";
    preserving.insert(ss.str());
  }
  return preserving;
}

Result<SharedFD> SetLogger(std::string runtime_dir_parent) {
  SharedFD log_file;
  if (InSandbox()) {
    log_file = SharedFD::Open(
        runtime_dir_parent + "/instances/cvd-1/logs/launcher.log",
        O_WRONLY | O_APPEND);
  } else {
    while (runtime_dir_parent[runtime_dir_parent.size() - 1] == '/') {
      runtime_dir_parent =
          runtime_dir_parent.substr(0, FLAGS_instance_dir.rfind('/'));
    }
    runtime_dir_parent =
        runtime_dir_parent.substr(0, FLAGS_instance_dir.rfind('/'));
    log_file = SharedFD::Open(runtime_dir_parent, O_WRONLY | O_TMPFILE,
                              S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  }
  if (!log_file->IsOpen()) {
    LOG(ERROR) << "Could not open initial log file: " << log_file->StrError();
  } else {
    android::base::SetLogger(TeeLogger({
        {ConsoleSeverity(), SharedFD::Dup(2), MetadataLevel::ONLY_MESSAGE},
        {LogFileSeverity(), log_file, MetadataLevel::FULL},
    }));
  }
  return log_file;
}

Result<const CuttlefishConfig*> InitFilesystemAndCreateConfig(
    FetcherConfigs fetcher_configs,
    const std::vector<GuestConfig>& guest_configs, fruit::Injector<>& injector,
    SharedFD log, const BootImageFlag& boot_image,
    const InitramfsPathFlag& initramfs_path, const KernelPathFlag& kernel_path,
    const SuperImageFlag& super_image,
    const SystemImageDirFlag& system_image_dir,
    const VendorBootImageFlag& vendor_boot_image,
    const VmManagerFlag& vm_manager_flag, const Defaults& defaults) {
  {
    // The config object is created here, but only exists in memory until the
    // SaveConfig line below. Don't launch cuttlefish subprocesses between these
    // two operations, as those will assume they can read the config object from
    // disk.
    auto config = CF_EXPECT(
        InitializeCuttlefishConfiguration(
            FLAGS_instance_dir, guest_configs, injector, fetcher_configs,
            boot_image, initramfs_path, kernel_path, super_image,
            system_image_dir, vendor_boot_image, vm_manager_flag, defaults),
        "cuttlefish configuration initialization failed");

    const std::string snapshot_path = FLAGS_snapshot_path;
    if (!snapshot_path.empty()) {
      CF_EXPECT(RestoreHostFiles(config.root_dir(), snapshot_path));

      // Add a delimiter to each log file so that we can clearly tell what
      // happened before vs after the restore.
      const std::string snapshot_delimiter =
          "\n\n\n"
          "============ SNAPSHOT RESTORE POINT ============\n"
          "Lines above are pre-snapshot.\n"
          "Lines below are post-restore.\n"
          "================================================\n"
          "\n\n\n";
      for (const auto& instance : config.Instances()) {
        const auto log_files =
            CF_EXPECT(DirectoryContents(instance.PerInstanceLogPath("")));
        for (const auto& filename : log_files) {
          const std::string path = instance.PerInstanceLogPath(filename);
          auto fd = SharedFD::Open(path, O_WRONLY | O_APPEND);
          CF_EXPECT(fd->IsOpen(),
                    "failed to open " << path << ": " << fd->StrError());
          const ssize_t n = WriteAll(fd, snapshot_delimiter);
          CF_EXPECT(n == snapshot_delimiter.size(),
                    "failed to write to " << path << ": " << fd->StrError());
        }
      }
    }

    // take the max value of modem_simulator_instance_number in each instance
    // which is used for preserving/deleting iccprofile_for_simX.xml files
    int modem_simulator_count = 0;

    bool creating_os_disk = false;
    // if any device needs to rebuild its composite disk,
    // then don't preserve any files and delete everything.
    for (const auto& instance : config.Instances()) {
      Result<MetadataImage> metadata = MetadataImage::Reuse(instance);
      Result<MiscImage> misc = MiscImage::Reuse(instance);
      Result<std::optional<ChromeOsStateImage>> chrome_os_state =
          CF_EXPECT(ChromeOsStateImage::Reuse(instance));
      if (chrome_os_state.ok() && metadata.ok() && misc.ok()) {
        DiskBuilder os_builder =
            OsCompositeDiskBuilder(config, instance, *chrome_os_state,
                                   *metadata, *misc, system_image_dir);
        creating_os_disk |= CF_EXPECT(os_builder.WillRebuildCompositeDisk());
      } else {
        creating_os_disk = true;
        break;
      }
      if (instance.ap_boot_flow() != APBootFlow::None) {
        auto ap_builder = ApCompositeDiskBuilder(config, instance);
        creating_os_disk |= CF_EXPECT(ap_builder.WillRebuildCompositeDisk());
      }
      if (instance.modem_simulator_instance_number() > modem_simulator_count) {
        modem_simulator_count = instance.modem_simulator_instance_number();
      }
    }
    // TODO(schuffelen): Add smarter decision for when to delete runtime files.
    // Files like NVChip are tightly bound to Android keymint and should be
    // deleted when userdata is reset. However if the user has ever run without
    // the overlay, then we want to keep this until userdata.img was externally
    // replaced.
    creating_os_disk &= FLAGS_use_overlay;

    std::set<std::string> preserving =
        CF_EXPECT(PreservingOnResume(creating_os_disk, modem_simulator_count),
                  "Error in Preserving set calculation.");
    auto instance_dirs = config.instance_dirs();
    auto environment_dirs = config.environment_dirs();
    std::vector<std::string> clean_dirs;
    clean_dirs.push_back(config.assembly_dir());
    clean_dirs.insert(clean_dirs.end(), instance_dirs.begin(),
                      instance_dirs.end());
    clean_dirs.insert(clean_dirs.end(), environment_dirs.begin(),
                      environment_dirs.end());
    CF_EXPECT(CleanPriorFiles(preserving, clean_dirs),
              "Failed to clean prior files");

    auto default_group = "cvdnetwork";
    const mode_t default_mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;

    CF_EXPECT(EnsureDirectoryExists(config.root_dir()));
    CF_EXPECT(EnsureDirectoryExists(config.assembly_dir()));
    CF_EXPECT(EnsureDirectoryExists(config.instances_dir()));
    CF_EXPECT(EnsureDirectoryExists(config.instances_uds_dir(), default_mode,
                                    default_group));
    CF_EXPECT(EnsureDirectoryExists(config.environments_dir(), default_mode,
                                    default_group));
    CF_EXPECT(EnsureDirectoryExists(config.environments_uds_dir(), default_mode,
                                    default_group));
    if (!snapshot_path.empty()) {
      SharedFD temp = SharedFD::Creat(config.AssemblyPath("restore"), 0660);
      if (!temp->IsOpen()) {
        return CF_ERR("Failed to create restore file: " << temp->StrError());
      }
    }

    auto environment =
        const_cast<const CuttlefishConfig&>(config).ForDefaultEnvironment();

    CF_EXPECT(EnsureDirectoryExists(environment.environment_dir(), default_mode,
                                    default_group));
    CF_EXPECT(EnsureDirectoryExists(environment.environment_uds_dir(),
                                    default_mode, default_group));
    CF_EXPECT(EnsureDirectoryExists(environment.PerEnvironmentLogPath(""),
                                    default_mode, default_group));
    CF_EXPECT(
        EnsureDirectoryExists(environment.PerEnvironmentGrpcSocketPath(""),
                              default_mode, default_group));

    LOG(INFO) << "Path for instance UDS: " << config.instances_uds_dir();

    if (log->LinkAtCwd(config.AssemblyPath("assemble_cvd.log"))) {
      LOG(ERROR) << "Unable to persist assemble_cvd log at "
                  << config.AssemblyPath("assemble_cvd.log")
                  << ": " << log->StrError();
    }
    for (const auto& instance : config.Instances()) {
      // Create instance directory if it doesn't exist.
      CF_EXPECT(EnsureDirectoryExists(instance.instance_dir()));
      auto internal_dir = instance.instance_dir() + "/" + kInternalDirName;
      CF_EXPECT(EnsureDirectoryExists(internal_dir));
      auto shared_dir = instance.instance_dir() + "/" + kSharedDirName;
      CF_EXPECT(EnsureDirectoryExists(shared_dir));
      auto recording_dir = instance.instance_dir() + "/recording";
      CF_EXPECT(EnsureDirectoryExists(recording_dir));
      CF_EXPECT(EnsureDirectoryExists(instance.PerInstanceLogPath("")));

      CF_EXPECT(EnsureDirectoryExists(instance.instance_uds_dir(), default_mode,
                                      default_group));
      CF_EXPECT(EnsureDirectoryExists(instance.instance_internal_uds_dir(),
                                      default_mode, default_group));
      CF_EXPECT(EnsureDirectoryExists(instance.PerInstanceGrpcSocketPath(""),
                                      default_mode, default_group));
      std::string vsock_dir = fmt::format("{}/vsock_{}_{}", TempDir(),
                                          instance.vsock_guest_cid(), getuid());
      if (DirectoryExists(vsock_dir, /* follow_symlinks */ false) &&
          !CF_EXPECT(IsDirectoryEmpty(vsock_dir))) {
        CF_EXPECT(RecursivelyRemoveDirectory(vsock_dir));
      }
      CF_EXPECT(EnsureDirectoryExists(vsock_dir, default_mode, default_group));

      // TODO(schuffelen): Move this code somewhere better
      CF_EXPECT(CreateLegacySymlinks(instance, environment));
    }
    CF_EXPECT(SaveConfig(config), "Failed to initialize configuration");
  }

  // Do this early so that the config object is ready for anything that needs
  // it
  auto config = CuttlefishConfig::Get();
  CF_EXPECT(config != nullptr, "Failed to obtain config singleton");

  if (DirectoryExists(FLAGS_assembly_dir, /* follow_symlinks */ false)) {
    CF_EXPECT(RecursivelyRemoveDirectory(FLAGS_assembly_dir));
  } else if (FileExists(FLAGS_assembly_dir, /* follow_symlinks */ false)) {
    CF_EXPECT(RemoveFile(FLAGS_assembly_dir),
              "Failed to remove file" << FLAGS_assembly_dir);
  }
  // TODO(schuffelen): Find alternative for host-sandboxing mode
  if (!InSandbox()) {
    CF_EXPECT(Symlink(config->assembly_dir(), FLAGS_assembly_dir));
  }

  std::string first_instance = config->Instances()[0].instance_dir();
  std::string double_legacy_instance_dir = FLAGS_instance_dir + "_runtime";
  if (FileExists(double_legacy_instance_dir, /* follow_symlinks */ false)) {
    CF_EXPECT(RemoveFile(double_legacy_instance_dir),
              "Failed to remove symlink " << double_legacy_instance_dir);
  }
  // TODO(schuffelen): Find alternative for host-sandboxing mode
  if (!InSandbox()) {
    CF_EXPECT(Symlink(first_instance, double_legacy_instance_dir));
  }

  CF_EXPECT(CreateDynamicDiskFiles(fetcher_configs, *config, system_image_dir));

  return config;
}

Result<void> VerifyConditionsOnSnapshotRestore(
    const std::string& snapshot_path) {
  if (snapshot_path.empty()) {
    return {};
  }
  const std::string instance_dir(FLAGS_instance_dir);
  const std::string assembly_dir(FLAGS_assembly_dir);
  CF_EXPECT(snapshot_path.empty() || FLAGS_resume,
            "--resume must be true when restoring from snapshot.");
  CF_EXPECT_EQ(instance_dir, CF_DEFAULTS_INSTANCE_DIR,
               "--snapshot_path does not allow customizing --instance_dir");
  CF_EXPECT_EQ(assembly_dir, CF_DEFAULTS_ASSEMBLY_DIR,
               "--snapshot_path does not allow customizing --assembly_dir");
  return {};
}

fruit::Component<> FlagsComponent(SystemImageDirFlag* system_image_dir) {
  return fruit::createComponent()
      .bindInstance(*system_image_dir)
      .install(AdbConfigComponent)
      .install(AdbConfigFlagComponent)
      .install(AdbConfigFragmentComponent)
      .install(DisplaysConfigsComponent)
      .install(DisplaysConfigsFlagComponent)
      .install(DisplaysConfigsFragmentComponent)
      .install(TouchpadsConfigsComponent)
      .install(TouchpadsConfigsFlagComponent)
      .install(FastbootConfigComponent)
      .install(FastbootConfigFlagComponent)
      .install(FastbootConfigFragmentComponent)
      .install(GflagsComponent)
      .install(ConfigFlagComponent)
      .install(CustomActionsComponent);
}

Result<void> CheckNoTTY() {
  int tty = isatty(0);
  int error_num = errno;
  CF_EXPECT(tty == 0,
            "stdin was a tty, expected to be passed the output of a "
            "previous stage. Did you mean to run launch_cvd?");
  CF_EXPECT(error_num != EBADF,
            "stdin was not a valid file descriptor, expected to be "
            "passed the output of launch_cvd. Did you mean to run launch_cvd?");
  return {};
}

Result<std::vector<std::string>> ReadInputFiles() {
  std::string input_files_str;
  auto input_fd = SharedFD::Dup(0);
  CF_EXPECTF(input_fd->IsOpen(), "Failed to dup stdin: {}",
             input_fd->StrError());
  auto bytes_read = ReadAll(input_fd, &input_files_str);
  CF_EXPECT(bytes_read >= 0, "Failed to read input files. Error was \""
                                 << input_fd->StrError() << "\"");
  return android::base::Split(input_files_str, "\n");
}

} // namespace

Result<int> AssembleCvdMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  auto log = CF_EXPECT(SetLogger(AbsolutePath(FLAGS_instance_dir)));

  CF_EXPECT(CheckNoTTY());

  // Read everything that cvd_internal_start writes, but ignore it since
  // fetcher_config.json will be searched for in the system image directory.
  (void) CF_EXPECT(ReadInputFiles());

  auto args = ArgsToVec(argc - 1, argv + 1);

  bool help = false;
  std::string help_str;
  bool helpxml = false;

  std::vector<Flag> help_flags = {
      GflagsCompatFlag("help", help),
      GflagsCompatFlag("helpfull", help),
      GflagsCompatFlag("helpshort", help),
      GflagsCompatFlag("helpmatch", help_str),
      GflagsCompatFlag("helpon", help_str),
      GflagsCompatFlag("helppackage", help_str),
      GflagsCompatFlag("helpxml", helpxml),
  };
  for (const auto& help_flag : help_flags) {
    CF_EXPECT(help_flag.Parse(args), "Failed to process help flag");
  }

  {
    std::string process_name = "assemble_cvd";
    std::vector<char*> pseudo_argv = {process_name.data()};
    for (auto& arg : args) {
      pseudo_argv.push_back(arg.data());
    }
    int argc = pseudo_argv.size();
    auto argv = pseudo_argv.data();
    gflags::AllowCommandLineReparsing();  // Support future non-gflags flags
    gflags::ParseCommandLineNonHelpFlags(&argc, &argv,
                                         /* remove_flags */ false);
  }

  SystemImageDirFlag system_image_dir =
      CF_EXPECT(SystemImageDirFlag::FromGlobalGflags());

  FetcherConfigs fetcher_configs = FindFetcherConfigs(system_image_dir);

  InitramfsPathFlag initramfs_path =
      InitramfsPathFlag::FromGlobalGflags(fetcher_configs);
  KernelPathFlag kernel_path = KernelPathFlag::FromGlobalGflags(fetcher_configs);

  BootImageFlag boot_image = BootImageFlag::FromGlobalGflags(system_image_dir);
  SuperImageFlag super_image =
      SuperImageFlag::FromGlobalGflags(system_image_dir);

  VendorBootImageFlag vendor_boot_image =
      VendorBootImageFlag::FromGlobalGflags(system_image_dir);

  fruit::Injector<> injector(FlagsComponent, &system_image_dir);

  for (auto& late_injected : injector.getMultibindings<LateInjected>()) {
    CF_EXPECT(late_injected->LateInject(injector));
  }

  auto flag_features = injector.getMultibindings<FlagFeature>();
  CF_EXPECT(FlagFeature::ProcessFlags(flag_features, args),
            "Failed to parse flags.");

  if (help || !help_str.empty()) {
    LOG(WARNING) << "TODO(schuffelen): Implement `--help` for assemble_cvd.";
    LOG(WARNING) << "In the meantime, call `launch_cvd --help`";
    return 1;
  } else if (helpxml) {
    if (!FlagFeature::WriteGflagsHelpXml(flag_features, std::cout)) {
      LOG(ERROR) << "Failure in writing gflags helpxml output";
    }
    return 1;  // For parity with gflags
  }

  CF_EXPECT(VerifyConditionsOnSnapshotRestore(FLAGS_snapshot_path),
            "The conditions for --snapshot_path=<dir> do not meet.");

  // TODO(schuffelen): Put in "unknown flag" guards after gflags is removed.
  // gflags either consumes all arguments that start with - or leaves all of
  // them in place, and either errors out on unknown flags or accepts any flags.

  CF_EXPECT(
      ResolveInstanceFiles(boot_image, initramfs_path, kernel_path, super_image,
                           system_image_dir, vendor_boot_image),
      "Failed to resolve instance files");
  // Depends on ResolveInstanceFiles to set flag globals
  std::vector<GuestConfig> guest_configs =
      CF_EXPECT(ReadGuestConfig(boot_image, kernel_path, system_image_dir));

  VmManagerFlag vm_manager_flag =
      CF_EXPECT(VmManagerFlag::FromGlobalGflags(guest_configs));

  CF_EXPECT(
      SetFlagDefaultsForVmm(guest_configs, system_image_dir, vm_manager_flag));

  Result<Defaults> defaults = GetFlagDefaultsFromConfig();
  if (!defaults.ok()) {
    LOG(FATAL) << "assemble_cvd: Couldn't get flag defaults from config; "
                  "aborting: "
               << defaults.error().Message();
  }
  auto config = CF_EXPECT(
      InitFilesystemAndCreateConfig(
          std::move(fetcher_configs), guest_configs, injector, log, boot_image,
          initramfs_path, kernel_path, super_image, system_image_dir,
          vendor_boot_image, vm_manager_flag, *defaults),
      "Failed to create config");

  std::cout << GetConfigFilePath(*config) << "\n";
  std::cout << std::flush;

  return 0;
}

} // namespace cuttlefish

int main(int argc, char** argv) {
  auto res = cuttlefish::AssembleCvdMain(argc, argv);
  if (res.ok()) {
    return *res;
  }
  LOG(ERROR) << "assemble_cvd failed: \n" << res.error().FormatForEnv();
  abort();
}
