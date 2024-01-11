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

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/assemble_cvd/clean.h"
#include "host/commands/assemble_cvd/disk_flags.h"
#include "host/commands/assemble_cvd/display.h"
#include "host/commands/assemble_cvd/flag_feature.h"
#include "host/commands/assemble_cvd/flags.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/assemble_cvd/touchpad.h"
#include "host/libs/command_util/snapshot_utils.h"
#include "host/libs/config/adb/adb.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/custom_actions.h"
#include "host/libs/config/fastboot/fastboot.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/config/inject.h"

using cuttlefish::StringFromEnv;

DEFINE_string(assembly_dir, CF_DEFAULTS_ASSEMBLY_DIR,
              "A directory to put generated files common between instances");
DEFINE_string(instance_dir, CF_DEFAULTS_INSTANCE_DIR,
              "This is a directory that will hold the cuttlefish generated"
              "files, including both instance-specific and common files");
DEFINE_string(snapshot_path, "",
              "Path to snapshot. Must not be empty if the device is to be "
              "restored from a snapshot");
DEFINE_bool(resume, CF_DEFAULTS_RESUME,
            "Resume using the disk from the last session, if "
            "possible. i.e., if --noresume is passed, the disk "
            "will be reset to the state it was initially launched "
            "in. This flag is ignored if the underlying partition "
            "images have been updated since the first launch."
            "If the device starts from a snapshot, this will be always true.");

DECLARE_bool(use_overlay);

namespace cuttlefish {
namespace {

static constexpr std::string_view kFetcherConfigFile = "fetcher_config.json";

FetcherConfig FindFetcherConfig(const std::vector<std::string>& files) {
  FetcherConfig fetcher_config;
  for (const auto& file : files) {
    if (android::base::EndsWith(file, kFetcherConfigFile)) {
      std::string home_directory = StringFromEnv("HOME", CurrentDirectory());
      std::string fetcher_file = file;
      if (!FileExists(file) &&
          FileExists(home_directory + "/" + fetcher_file)) {
        LOG(INFO) << "Found " << fetcher_file << " in HOME directory ('"
                  << home_directory << "') and not current working directory";
        fetcher_file = home_directory + "/" + fetcher_file;
      }

      if (fetcher_config.LoadFromFile(fetcher_file)) {
        return fetcher_config;
      }
      LOG(ERROR) << "Could not load fetcher config file.";
    }
  }
  LOG(DEBUG) << "Could not locate fetcher config file.";
  return fetcher_config;
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
  if (symlink(config_file.c_str(), config_link.c_str()) != 0) {
    return CF_ERRNO("symlink(\"" << config_file << "\", \"" << config_link
                                 << ") failed");
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
    if (symlink(log_target.c_str(), symlink_location.c_str()) != 0) {
      return CF_ERRNO("symlink(\"" << log_target << ", " << symlink_location
                                   << ") failed");
    }
  }

  std::stringstream legacy_instance_path_stream;
  legacy_instance_path_stream << FLAGS_instance_dir;
  if (gflags::GetCommandLineFlagInfoOrDie("instance_dir").is_default) {
    legacy_instance_path_stream << "_runtime";
  }
  legacy_instance_path_stream << "." << instance.id();
  auto legacy_instance_path = legacy_instance_path_stream.str();

  if (DirectoryExists(legacy_instance_path, /* follow_symlinks */ false)) {
    CF_EXPECT(RecursivelyRemoveDirectory(legacy_instance_path),
              "Failed to remove legacy directory " << legacy_instance_path);
  } else if (FileExists(legacy_instance_path, /* follow_symlinks */ false)) {
    CF_EXPECT(RemoveFile(legacy_instance_path),
              "Failed to remove instance_dir symlink " << legacy_instance_path);
  }
  if (symlink(instance.instance_dir().c_str(), legacy_instance_path.c_str())) {
    return CF_ERRNO("symlink(\"" << instance.instance_dir() << "\", \""
                                 << legacy_instance_path << "\") failed");
  }

  const auto mac80211_uds_name = "vhost_user_mac80211";

  const auto mac80211_uds_path =
      environment.PerEnvironmentUdsPath(mac80211_uds_name);
  const auto legacy_mac80211_uds_path =
      instance.PerInstanceInternalPath(mac80211_uds_name);

  if (symlink(mac80211_uds_path.c_str(), legacy_mac80211_uds_path.c_str())) {
    return CF_ERRNO("symlink(\"" << mac80211_uds_path << "\", \""
                                 << legacy_mac80211_uds_path << "\") failed");
  }

  return {};
}

Result<void> RestoreHostFiles(const std::string& cuttlefish_root_dir,
                              const std::string& snapshot_dir_path) {
  const auto meta_json_path = SnapshotMetaJsonPath(snapshot_dir_path);

  auto guest_snapshot_dirs =
      CF_EXPECT(GuestSnapshotDirectories(snapshot_dir_path));
  auto filter_guest_dir =
      [&guest_snapshot_dirs](const std::string& src_dir) -> bool {
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
    return std::set<std::string>{};
  }
  CF_EXPECT(snapshot_path.empty() || !creating_os_disk,
            "Restoring from snapshot requires not creating OS disks");
  if (creating_os_disk) {
    // not snapshot restore, must be --resume
    LOG(INFO) << "Requested resuming a previous session (the default behavior) "
              << "but the base images have changed under the overlay, making "
              << "the overlay incompatible. Wiping the overlay files.";
    return std::set<std::string>{};
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
  preserving.insert("uboot_env.img");
  preserving.insert("factory_reset_protected.img");
  preserving.insert("misc.img");
  preserving.insert("metadata.img");
  preserving.insert("vbmeta.img");
  preserving.insert("oemlock_secure");
  preserving.insert("oemlock_insecure");
  for (int i = 0; i < modem_simulator_count; i++) {
    std::stringstream ss;
    ss << "iccprofile_for_sim" << i << ".xml";
    preserving.insert(ss.str());
  }
  return preserving;
}

Result<const CuttlefishConfig*> InitFilesystemAndCreateConfig(
    FetcherConfig fetcher_config, const std::vector<GuestConfig>& guest_configs,
    fruit::Injector<>& injector) {
  std::string runtime_dir_parent = AbsolutePath(FLAGS_instance_dir);
  while (runtime_dir_parent[runtime_dir_parent.size() - 1] == '/') {
    runtime_dir_parent =
        runtime_dir_parent.substr(0, FLAGS_instance_dir.rfind('/'));
  }
  runtime_dir_parent =
      runtime_dir_parent.substr(0, FLAGS_instance_dir.rfind('/'));
  auto log = SharedFD::Open(runtime_dir_parent, O_WRONLY | O_TMPFILE,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (!log->IsOpen()) {
    LOG(ERROR) << "Could not open O_TMPFILE precursor to assemble_cvd.log: "
               << log->StrError();
  } else {
    android::base::SetLogger(TeeLogger({
        {ConsoleSeverity(), SharedFD::Dup(2), MetadataLevel::ONLY_MESSAGE},
        {LogFileSeverity(), log, MetadataLevel::FULL},
    }));
  }

  {
    // The config object is created here, but only exists in memory until the
    // SaveConfig line below. Don't launch cuttlefish subprocesses between these
    // two operations, as those will assume they can read the config object from
    // disk.
    auto config = CF_EXPECT(
        InitializeCuttlefishConfiguration(FLAGS_instance_dir, guest_configs,
                                          injector, fetcher_config),
        "cuttlefish configuration initialization failed");

    const std::string snapshot_path = FLAGS_snapshot_path;
    if (!snapshot_path.empty()) {
      CF_EXPECT(RestoreHostFiles(config.root_dir(), snapshot_path));
    }

    // take the max value of modem_simulator_instance_number in each instance
    // which is used for preserving/deleting iccprofile_for_simX.xml files
    int modem_simulator_count = 0;

    bool creating_os_disk = false;
    // if any device needs to rebuild its composite disk,
    // then don't preserve any files and delete everything.
    for (const auto& instance : config.Instances()) {
      auto os_builder = OsCompositeDiskBuilder(config, instance);
      creating_os_disk |= CF_EXPECT(os_builder.WillRebuildCompositeDisk());
      if (instance.ap_boot_flow() != CuttlefishConfig::InstanceSpecific::APBootFlow::None) {
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
      auto vsock_dir =
          fmt::format("/tmp/vsock_{0}_{1}", instance.vsock_guest_cid(),
                      std::to_string(getuid()));
      RecursivelyRemoveDirectory(vsock_dir);
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
    CF_EXPECT(RecursivelyRemoveDirectory(FLAGS_assembly_dir),
              "Failed to remove directory " << FLAGS_assembly_dir);
  } else if (FileExists(FLAGS_assembly_dir, /* follow_symlinks */ false)) {
    CF_EXPECT(RemoveFile(FLAGS_assembly_dir),
              "Failed to remove file" << FLAGS_assembly_dir);
  }
  if (symlink(config->assembly_dir().c_str(),
              FLAGS_assembly_dir.c_str())) {
    return CF_ERRNO("symlink(\"" << config->assembly_dir() << "\", \""
                                 << FLAGS_assembly_dir << "\") failed");
  }

  std::string first_instance = config->Instances()[0].instance_dir();
  std::string double_legacy_instance_dir = FLAGS_instance_dir + "_runtime";
  if (FileExists(double_legacy_instance_dir, /* follow_symlinks */ false)) {
    CF_EXPECT(RemoveFile(double_legacy_instance_dir),
              "Failed to remove symlink " << double_legacy_instance_dir);
  }
  if (symlink(first_instance.c_str(), double_legacy_instance_dir.c_str())) {
    return CF_ERRNO("symlink(\"" << first_instance << "\", \""
                                 << double_legacy_instance_dir
                                 << "\") failed");
  }

  CF_EXPECT(CreateDynamicDiskFiles(fetcher_config, *config));

  return config;
}

const std::string kKernelDefaultPath = "kernel";
const std::string kInitramfsImg = "initramfs.img";
static void ExtractKernelParamsFromFetcherConfig(
    const FetcherConfig& fetcher_config) {
  std::string discovered_kernel =
      fetcher_config.FindCvdFileWithSuffix(kKernelDefaultPath);
  std::string discovered_ramdisk =
      fetcher_config.FindCvdFileWithSuffix(kInitramfsImg);

  SetCommandLineOptionWithMode("kernel_path", discovered_kernel.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  SetCommandLineOptionWithMode("initramfs_path", discovered_ramdisk.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
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

fruit::Component<> FlagsComponent() {
  return fruit::createComponent()
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

} // namespace

Result<int> AssembleCvdMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  int tty = isatty(0);
  int error_num = errno;
  CF_EXPECT(tty == 0,
            "stdin was a tty, expected to be passed the output of a "
            "previous stage. Did you mean to run launch_cvd?");
  CF_EXPECT(error_num != EBADF,
            "stdin was not a valid file descriptor, expected to be "
            "passed the output of launch_cvd. Did you mean to run launch_cvd?");

  std::string input_files_str;
  {
    auto input_fd = SharedFD::Dup(0);
    auto bytes_read = ReadAll(input_fd, &input_files_str);
    CF_EXPECT(bytes_read >= 0, "Failed to read input files. Error was \""
                                   << input_fd->StrError() << "\"");
  }
  std::vector<std::string> input_files = android::base::Split(input_files_str, "\n");

  FetcherConfig fetcher_config = FindFetcherConfig(input_files);

  // set gflags defaults to point to kernel/RD from fetcher config
  ExtractKernelParamsFromFetcherConfig(fetcher_config);

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

  fruit::Injector<> injector(FlagsComponent);

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

  auto guest_configs =
      CF_EXPECT(GetGuestConfigAndSetDefaults(), "Failed to parse arguments");

  auto config =
      CF_EXPECT(InitFilesystemAndCreateConfig(std::move(fetcher_config),
                                              guest_configs, injector),
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
