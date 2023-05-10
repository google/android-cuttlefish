/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/acloud/converter.h"

#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <optional>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <google/protobuf/text_format.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/acloud/config.h"
#include "host/commands/cvd/acloud/create_converter_parser.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/instance_lock.h"  // TempDir()
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

// Image names to search
const std::vector<std::string> _KERNEL_IMAGE_NAMES = {"kernel", "bzImage",
                                                      "Image"};
const std::vector<std::string> _INITRAMFS_IMAGE_NAME = {"initramfs.img"};
const std::vector<std::string> _BOOT_IMAGE_NAME = {"boot.img"};
const std::vector<std::string> _VENDOR_BOOT_IMAGE_NAME = {"vendor_boot.img"};
const std::string _MIXED_SUPER_IMAGE_NAME = "mixed_super.img";

/**
 * Split a string into arguments based on shell tokenization rules.
 *
 * This behaves like `shlex.split` from python where arguments are separated
 * based on whitespace, but quoting and quote escaping is respected. This
 * function effectively removes one level of quoting from its inputs while
 * making the split.
 */
Result<std::vector<std::string>> BashTokenize(const std::string& str) {
  Command command("bash");
  command.AddParameter("-c");
  command.AddParameter("printf '%s\n' ", str);
  std::string stdout_str;
  std::string stderr_str;
  auto ret = RunWithManagedStdio(std::move(command), nullptr, &stdout_str,
                                 &stderr_str);
  CF_EXPECT(ret == 0,
            "printf fail \"" << stdout_str << "\", \"" << stderr_str << "\"");
  return android::base::Split(stdout_str, "\n");
}

}  // namespace

// body of pure virtual destructor required by C++
ConvertAcloudCreateCommand::~ConvertAcloudCreateCommand() {}

class ConvertAcloudCreateCommandImpl : public ConvertAcloudCreateCommand {
 public:
  INJECT(ConvertAcloudCreateCommandImpl()) {}
  ~ConvertAcloudCreateCommandImpl() override = default;

  Result<ConvertedAcloudCreateCommand> Convert(
      const RequestWithStdio& request) {
    auto arguments = ParseInvocation(request.Message()).arguments;
    CF_EXPECT(arguments.size() > 0);
    CF_EXPECT(arguments[0] == "create");
    arguments.erase(arguments.begin());

    /*
     * TODO(chadreynolds@): Move all the flag parsing eventually to the
     * converter_parser.{h,cpp}.
     *
     * Note that the transfer should be done from the top through the bottom.
     * ParseFlags() parses each flag in order.
     */
    auto parsed_flags =
        CF_EXPECT(acloud_impl::ParseAcloudCreateFlags(arguments));

    std::vector<Flag> flags;

    std::optional<std::string> local_kernel_image;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--local-kernel-image"})
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--local-boot-image"})
                           .Setter([&local_kernel_image](const FlagMatch& m) {
                             local_kernel_image = m.value;
                             return true;
                           }));

    std::optional<std::string> image_download_dir;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--image-download-dir"})
                           .Setter([&image_download_dir](const FlagMatch& m) {
                             image_download_dir = m.value;
                             return true;
                           }));

    std::optional<std::string> local_system_image;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--local-system-image"})
            .Setter([&local_system_image](const FlagMatch& m) {
              local_system_image = m.value;
              return true;
            }));

    verbose_ = false;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagExact, "-v"})
                           .Alias({FlagAliasMode::kFlagExact, "-vv"})
                           .Alias({FlagAliasMode::kFlagExact, "--verbose"})
                           .Setter([this](const FlagMatch&) {
                             verbose_ = true;
                             return true;
                           }));

    std::optional<std::string> branch;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--branch"})
            .Setter([&branch](const FlagMatch& m) {
              branch = m.value;
              return true;
            }));

    bool local_image;
    std::optional<std::string> local_image_path;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesArbitrary, "--local-image"})
            .Setter([&local_image,
                     &local_image_path](const FlagMatch& m) {
              local_image = true;
              if (m.value != "") {
                local_image_path = m.value;
              }
              return true;
            }));

    std::optional<std::string> build_id;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--build-id"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--build_id"})
            .Setter([&build_id](const FlagMatch& m) {
              build_id = m.value;
              return true;
            }));

    std::optional<std::string> build_target;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--build-target"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--build_target"})
            .Setter([&build_target](const FlagMatch& m) {
              build_target = m.value;
              return true;
            }));

    std::optional<std::string> config_file;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--config-file"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--config_file"})
            .Setter([&config_file](const FlagMatch& m) {
              config_file = m.value;
              return true;
            }));

    std::optional<std::string> bootloader_build_id;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--bootloader-build-id"})
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--bootloader_build_id"})
                           .Setter([&bootloader_build_id](const FlagMatch& m) {
                             bootloader_build_id = m.value;
                             return true;
                           }));
    std::optional<std::string> bootloader_build_target;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing,
                    "--bootloader-build-target"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing,
                    "--bootloader_build_target"})
            .Setter([&bootloader_build_target](const FlagMatch& m) {
              bootloader_build_target = m.value;
              return true;
            }));
    std::optional<std::string> bootloader_branch;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--bootloader-branch"})
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--bootloader_branch"})
                           .Setter([&bootloader_branch](const FlagMatch& m) {
                             bootloader_branch = m.value;
                             return true;
                           }));

    std::optional<std::string> boot_build_id;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--boot-build-id"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--boot_build_id"})
            .Setter([&boot_build_id](const FlagMatch& m) {
              boot_build_id = m.value;
              return true;
            }));
    std::optional<std::string> boot_build_target;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--boot-build-target"})
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--boot_build_target"})
                           .Setter([&boot_build_target](const FlagMatch& m) {
                             boot_build_target = m.value;
                             return true;
                           }));
    std::optional<std::string> boot_branch;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--boot-branch"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--boot_branch"})
            .Setter([&boot_branch](const FlagMatch& m) {
              boot_branch = m.value;
              return true;
            }));
    std::optional<std::string> boot_artifact;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--boot-artifact"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--boot_artifact"})
            .Setter([&boot_artifact](const FlagMatch& m) {
              boot_artifact = m.value;
              return true;
            }));

    std::optional<std::string> ota_build_id;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--ota-build-id"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--ota_build_id"})
            .Setter([&ota_build_id](const FlagMatch& m) {
              ota_build_id = m.value;
              return true;
            }));
    std::optional<std::string> ota_build_target;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--ota-build-target"})
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--ota_build_target"})
                           .Setter([&ota_build_target](const FlagMatch& m) {
                             ota_build_target = m.value;
                             return true;
                           }));
    std::optional<std::string> ota_branch;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--ota-branch"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--ota_branch"})
            .Setter([&ota_branch](const FlagMatch& m) {
              ota_branch = m.value;
              return true;
            }));

    std::optional<std::string> launch_args;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--launch-args"})
            .Setter([&launch_args](const FlagMatch& m) {
              launch_args = m.value;
              return true;
            }));

    std::optional<std::string> system_branch;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--system-branch"})
            .Setter([&system_branch](const FlagMatch& m) {
              system_branch = m.value;
              return true;
            }));

    std::optional<std::string> system_build_target;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--system-build-target"})
                           .Setter([&system_build_target](const FlagMatch& m) {
                             system_build_target = m.value;
                             return true;
                           }));

    std::optional<std::string> system_build_id;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--system-build-id"})
            .Setter([&system_build_id](const FlagMatch& m) {
              system_build_id = m.value;
              return true;
            }));

    std::optional<std::string> kernel_branch;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--kernel-branch"})
            .Setter([&kernel_branch](const FlagMatch& m) {
              kernel_branch = m.value;
              return true;
            }));

    std::optional<std::string> kernel_build_target;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--kernel-build-target"})
                           .Setter([&kernel_build_target](const FlagMatch& m) {
                             kernel_build_target = m.value;
                             return true;
                           }));

    std::optional<std::string> kernel_build_id;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--kernel-build-id"})
            .Setter([&kernel_build_id](const FlagMatch& m) {
              kernel_build_id = m.value;
              return true;
            }));

    std::optional<std::string> pet_name;
    Flag pet_name_gflag = GflagsCompatFlag("pet-name");
    flags.emplace_back(
        GflagsCompatFlag("pet-name")
            .Getter([&pet_name]() { return (pet_name ? *pet_name : ""); })
            .Setter([&pet_name](const FlagMatch& match) {
              pet_name = match.value;
              return true;
            }));

    CF_EXPECT(ParseFlags(flags, arguments));
    CF_EXPECT(arguments.size() == 0,
              "Unrecognized arguments:'"
                  << android::base::Join(arguments, "', '") << "'");

    CF_EXPECT(parsed_flags.local_instance_set == true,
              "Only '--local-instance' is supported");
    auto host_dir = TempDir() + "/acloud_image_artifacts/";
    if (image_download_dir) {
      host_dir = image_download_dir.value() + "/acloud_image_artifacts/";
    }

    const auto& request_command = request.Message().command_request();
    auto host_artifacts_path = request_command.env().find(kAndroidHostOut);
    CF_EXPECT(host_artifacts_path != request_command.env().end(),
              "Missing " << kAndroidHostOut);

    std::vector<cvd::Request> request_protos;
    const uid_t uid = request.Credentials()->uid;
    // default user config path
    std::string user_config_path = CF_EXPECT(GetDefaultConfigFile(uid));

    if (config_file) {
      user_config_path = config_file.value();
    }
    AcloudConfig acloud_config =
        CF_EXPECT(LoadAcloudConfig(user_config_path, uid));

    if (local_image) {
      CF_EXPECT(!(system_branch || system_build_target || system_build_id),
                "--local-image incompatible with --system-* flags");
      CF_EXPECT(!(bootloader_branch || bootloader_build_target ||
                  bootloader_build_id),
                "--local-image incompatible with --bootloader-* flags");
      CF_EXPECT(
          !(boot_branch || boot_build_target || boot_build_id || boot_artifact),
          "--local-image incompatible with --boot-* flags");
      CF_EXPECT(!(ota_branch || ota_build_target || ota_build_id),
                "--local-image incompatible with --ota-* flags");
    } else {
      if (!DirectoryExists(host_dir)) {
        // fetch/download directory doesn't exist, create directory
        cvd::Request& mkdir_request = request_protos.emplace_back();
        auto& mkdir_command = *mkdir_request.mutable_command_request();
        mkdir_command.add_args("cvd");
        mkdir_command.add_args("mkdir");
        mkdir_command.add_args("-p");
        mkdir_command.add_args(host_dir);
        auto& mkdir_env = *mkdir_command.mutable_env();
        mkdir_env[kAndroidHostOut] = host_artifacts_path->second;
      }
      if (branch || build_id || build_target) {
        auto target = build_target ? *build_target : "";
        auto build = build_id.value_or(branch.value_or("aosp-master"));
        host_dir += (build + target);
      } else {
        host_dir += "aosp-master";
      }
      // TODO(weihsu): if we fetch default ID such as aosp-master,
      // cvd fetch will fetch the latest release. There is a potential
      // issue that two different fetch with same default ID may
      // download different releases.
      // Eventually, we should match python acloud behavior to translate
      // default ID (aosp-master) to real ID to solve this issue.

      cvd::Request& fetch_request = request_protos.emplace_back();
      auto& fetch_command = *fetch_request.mutable_command_request();
      fetch_command.add_args("cvd");
      fetch_command.add_args("fetch");
      fetch_command.add_args("--directory");
      fetch_command.add_args(host_dir);
      fetch_command_str_ = "";
      if (branch || build_id || build_target) {
        fetch_command.add_args("--default_build");
        fetch_command_str_ += "--default_build=";
        auto target = build_target ? "/" + *build_target : "";
        auto build = build_id.value_or(branch.value_or("aosp-master"));
        fetch_command.add_args(build + target);
        fetch_command_str_ += (build + target);
      }
      if (system_branch || system_build_id || system_build_target) {
        fetch_command.add_args("--system_build");
        fetch_command_str_ += " --system_build=";
        auto target = system_build_target.value_or(build_target.value_or(""));
        if (target != "") {
          target = "/" + target;
        }
        auto build =
            system_build_id.value_or(system_branch.value_or("aosp-master"));
        fetch_command.add_args(build + target);
        fetch_command_str_ += (build + target);
      }
      if (bootloader_branch || bootloader_build_id || bootloader_build_target) {
        fetch_command.add_args("--bootloader_build");
        fetch_command_str_ += " --bootloader_build=";
        auto target = bootloader_build_target.value_or("");
        if (target != "") {
          target = "/" + target;
        }
        auto build = bootloader_build_id.value_or(
            bootloader_branch.value_or("aosp_u-boot-mainline"));
        fetch_command.add_args(build + target);
        fetch_command_str_ += (build + target);
      }
      if (boot_branch || boot_build_id || boot_build_target) {
        fetch_command.add_args("--boot_build");
        fetch_command_str_ += " --boot_build=";
        auto target = boot_build_target.value_or("");
        if (target != "") {
          target = "/" + target;
        }
        auto build =
            boot_build_id.value_or(boot_branch.value_or("aosp-master"));
        fetch_command.add_args(build + target);
        fetch_command_str_ += (build + target);
      }
      if (boot_artifact) {
        CF_EXPECT(boot_branch || boot_build_target || boot_build_id,
                  "--boot-artifact must combine with other --boot-* flags");
        fetch_command.add_args("--boot_artifact");
        fetch_command_str_ += " --boot_artifact=";
        auto target = boot_artifact.value_or("");
        fetch_command.add_args(target);
        fetch_command_str_ += (target);
      }
      if (ota_branch || ota_build_id || ota_build_target) {
        fetch_command.add_args("--otatools_build");
        fetch_command_str_ += " --otatools_build=";
        auto target = ota_build_target.value_or("");
        if (target != "") {
          target = "/" + target;
        }
        auto build = ota_build_id.value_or(ota_branch.value_or(""));
        fetch_command.add_args(build + target);
        fetch_command_str_ += (build + target);
      }
      if (kernel_branch || kernel_build_id || kernel_build_target) {
        fetch_command.add_args("--kernel_build");
        fetch_command_str_ += " --kernel_build=";
        auto target = kernel_build_target.value_or("kernel_virt_x86_64");
        auto build = kernel_build_id.value_or(
            kernel_branch.value_or("aosp_kernel-common-android-mainline"));
        fetch_command.add_args(build + "/" + target);
        fetch_command_str_ += (build + "/" + target);
      }
      auto& fetch_env = *fetch_command.mutable_env();
      fetch_env[kAndroidHostOut] = host_artifacts_path->second;

      fetch_cvd_args_file_ = host_dir + "/fetch-cvd-args.txt";
      if (FileExists(fetch_cvd_args_file_)) {
        // file exists
        std::string read_str;
        using android::base::ReadFileToString;
        CF_EXPECT(ReadFileToString(fetch_cvd_args_file_.c_str(), &read_str,
                                   /* follow_symlinks */ true));
        if (read_str == fetch_command_str_) {
          // same fetch cvd command, reuse original dir
          fetch_command_str_ = "";
          request_protos.pop_back();
        }
      }
    }

    std::string super_image_path = "";
    if (local_system_image) {
      // in new cvd server design, at this point,
      // we don't know which HOME is assigned by cvd start.
      // create a temporary directory to store generated
      // mix super image
      TemporaryDir my_dir;
      std::string required_paths;
      my_dir.DoNotRemove();
      super_image_path = std::string(my_dir.path) + "/" + _MIXED_SUPER_IMAGE_NAME;

      //combine super_image path and local_system_image path
      required_paths = super_image_path;
      required_paths += ("," + local_system_image.value());

      cvd::Request& mixsuperimage_request = request_protos.emplace_back();
      auto& mixsuperimage_command = *mixsuperimage_request.mutable_command_request();
      mixsuperimage_command.add_args("cvd");
      mixsuperimage_command.add_args("acloud");
      mixsuperimage_command.add_args("mix-super-image");
      mixsuperimage_command.add_args("--super_image");

      auto& mixsuperimage_env = *mixsuperimage_command.mutable_env();
      if (local_image) {
        if (local_image_path) {
          // added image_dir to required_paths for MixSuperImage use
          required_paths += ("," + local_image_path.value());
        } else {
          required_paths += ",";
        }

        mixsuperimage_env[kAndroidHostOut] = host_artifacts_path->second;

        auto product_out = request_command.env().find(kAndroidProductOut);
        CF_EXPECT(product_out != request_command.env().end(),
                  "Missing " << kAndroidProductOut);
        mixsuperimage_env[kAndroidProductOut] = product_out->second;
      } else {
        mixsuperimage_env[kAndroidHostOut] = host_dir;
        mixsuperimage_env[kAndroidProductOut] = host_dir;
      }

      mixsuperimage_command.add_args(required_paths);
    }

    cvd::Request start_request;
    auto& start_command = *start_request.mutable_command_request();
    start_command.add_args("cvd");
    start_command.add_args("start");
    start_command.add_args("--daemon");
    start_command.add_args("--undefok");
    start_command.add_args("report_anonymous_usage_stats");
    start_command.add_args("--report_anonymous_usage_stats");
    start_command.add_args("y");
    if (parsed_flags.flavor) {
      start_command.add_args("-config");
      start_command.add_args(parsed_flags.flavor.value());
    }

    if (local_system_image) {
      start_command.add_args("-super_image");
      start_command.add_args(super_image_path);
    }

    if (local_kernel_image) {
      // kernel image has 1st priority than boot image
      struct stat statbuf;
      std::string local_boot_image = "";
      std::string vendor_boot_image = "";
      std::string kernel_image = "";
      std::string initramfs_image = "";
      if (stat(local_kernel_image.value().c_str(), &statbuf) == 0) {
        if (statbuf.st_mode & S_IFDIR) {
          // it's a directory, deal with kernel image case first
          kernel_image =
              FindImage(local_kernel_image.value(), _KERNEL_IMAGE_NAMES);
          initramfs_image =
              FindImage(local_kernel_image.value(), _INITRAMFS_IMAGE_NAME);
          // This is the original python acloud behavior, it
          // expects both kernel and initramfs files, however,
          // there are some very old kernels that are built without
          // an initramfs.img file,
          // e.g. aosp_kernel-common-android-4.14-stable
          if (kernel_image != "" && initramfs_image != "") {
            start_command.add_args("-kernel_path");
            start_command.add_args(kernel_image);
            start_command.add_args("-initramfs_path");
            start_command.add_args(initramfs_image);
          } else {
            // boot.img case
            // adding boot.img and vendor_boot.img to the path
            local_boot_image =
                FindImage(local_kernel_image.value(), _BOOT_IMAGE_NAME);
            vendor_boot_image =
                FindImage(local_kernel_image.value(), _VENDOR_BOOT_IMAGE_NAME);
            start_command.add_args("-boot_image");
            start_command.add_args(local_boot_image);
            // vendor boot image may not exist
            if (vendor_boot_image != "") {
              start_command.add_args("-vendor_boot_image");
              start_command.add_args(vendor_boot_image);
            }
          }
        } else if (statbuf.st_mode & S_IFREG) {
          // it's a file which directly points to boot.img
          local_boot_image = local_kernel_image.value();
          start_command.add_args("-boot_image");
          start_command.add_args(local_boot_image);
        }
      }
    } else if (kernel_branch || kernel_build_id || kernel_build_target) {
      // fetch remote kernel image files
      std::string kernel_image = host_dir + "/kernel";

      // even if initramfs doesn't exist, launch_cvd will still handle it
      // correctly. We push the initramfs handler to launch_cvd stage.
      std::string initramfs_image = host_dir + "/initramfs.img";
      start_command.add_args("-kernel_path");
      start_command.add_args(kernel_image);
      start_command.add_args("-initramfs_path");
      start_command.add_args(initramfs_image);
    }

    if (launch_args) {
      for (const auto& arg : CF_EXPECT(BashTokenize(*launch_args))) {
        start_command.add_args(arg);
      }
    }
    if (acloud_config.launch_args != "") {
      for (const auto& arg :
           CF_EXPECT(BashTokenize(acloud_config.launch_args))) {
        start_command.add_args(arg);
      }
    }
    start_command.mutable_selector_opts()->add_args(
        std::string("--") + selector::SelectorFlags::kDisableDefaultGroup +
        "=true");
    if (pet_name) {
      const auto [group_name, instance_name] =
          CF_EXPECT(selector::BreakDeviceName(*pet_name),
                    *pet_name << " must be a group name followed by - "
                              << "followed by an instance name.");
      std::string group_name_arg = "--";
      group_name_arg.append(selector::SelectorFlags::kGroupName)
          .append("=")
          .append(group_name);
      std::string instance_name_arg = "--";
      instance_name_arg.append(selector::SelectorFlags::kInstanceName)
          .append("=")
          .append(instance_name);
      start_command.mutable_selector_opts()->add_args(group_name_arg);
      start_command.mutable_selector_opts()->add_args(instance_name_arg);
    }

    auto& start_env = *start_command.mutable_env();
    if (local_image) {
      if (local_image_path) {
        std::string local_image_path_str = local_image_path.value();
        // Python acloud source: local_image_local_instance.py;l=81
        // this acloud flag is equal to launch_cvd flag system_image_dir
        start_command.add_args("-system_image_dir");
        start_command.add_args(local_image_path_str);
      }

      start_env[kAndroidHostOut] = host_artifacts_path->second;

      auto product_out = request_command.env().find(kAndroidProductOut);
      CF_EXPECT(product_out != request_command.env().end(),
                "Missing " << kAndroidProductOut);
      start_env[kAndroidProductOut] = product_out->second;
    } else {
      start_env[kAndroidHostOut] = host_dir;
      start_env[kAndroidProductOut] = host_dir;
    }
    if (Contains(start_env, kCuttlefishInstanceEnvVarName)) {
      // Python acloud does not use this variable.
      // this variable will confuse cvd start, though
      start_env.erase(kCuttlefishInstanceEnvVarName);
    }
    if (parsed_flags.local_instance) {
      start_env[kCuttlefishInstanceEnvVarName] =
          std::to_string(*parsed_flags.local_instance);
    }
    // we don't know which HOME is assigned by cvd start.
    // cvd server does not rely on the working directory for cvd start
    *start_command.mutable_working_directory() =
        request_command.working_directory();
    std::vector<SharedFD> fds;
    if (verbose_) {
      fds = request.FileDescriptors();
    } else {
      auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
      CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
      fds = {dev_null, dev_null, dev_null};
    }

    ConvertedAcloudCreateCommand ret{
        .start_request = RequestWithStdio(request.Client(), start_request, fds,
                                          request.Credentials())};
    for (auto& request_proto : request_protos) {
      ret.prep_requests.emplace_back(request.Client(), request_proto, fds,
                                     request.Credentials());
    }
    return ret;
  }

  const std::string& FetchCvdArgsFile() const override {
    return fetch_cvd_args_file_;
  }

  const std::string& FetchCommandString() const override {
    return fetch_command_str_;
  }
  bool Verbose() const { return verbose_; }

 private:
  std::string fetch_cvd_args_file_;
  std::string fetch_command_str_;
  bool verbose_;
};

fruit::Component<ConvertAcloudCreateCommand>
AcloudCreateConvertCommandComponent() {
  return fruit::createComponent()
      .bind<ConvertAcloudCreateCommand, ConvertAcloudCreateCommandImpl>();
}

}  // namespace cuttlefish
