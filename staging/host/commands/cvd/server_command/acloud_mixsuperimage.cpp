/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <filesystem>
#include <fstream>
#include <iostream>

#include <android-base/file.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/acloud_mixsuperimage.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/config_utils.h"

namespace cuttlefish {

static constexpr char kMixSuperImageHelpMessage[] =
    R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd acloud mix-super-image <args>

Args:
  --super_image               Super image path.
)";

const std::string _MISC_INFO_FILE_NAME = "misc_info.txt";
const std::string _TARGET_FILES_META_DIR_NAME = "META";
const std::string _TARGET_FILES_IMAGES_DIR_NAME = "IMAGES";
const std::string _SYSTEM_IMAGE_NAME_PATTERN = "system.img";
const std::string _SYSTEM_EXT_IMAGE_NAME_PATTERN = "system_ext.img";
const std::string _PRODUCT_IMAGE_NAME_PATTERN = "product.img";

/*
 * Find misc info in build output dir or extracted target files.
 */
Result<std::string> FindMiscInfo(const std::string& image_dir) {
  std::string misc_info_path = image_dir + _MISC_INFO_FILE_NAME;

  if (FileExists(misc_info_path)) {
    return misc_info_path;
  }
  misc_info_path = image_dir + _TARGET_FILES_META_DIR_NAME +
                   "/" + _MISC_INFO_FILE_NAME;

  if (FileExists(misc_info_path)) {
    return misc_info_path;
  }
  return CF_ERR("Cannot find " << _MISC_INFO_FILE_NAME
                << " in " << image_dir);
}

/*
 * Find images in build output dir or extracted target files.
 */
Result<std::string> FindImageDir(const std::string& image_dir) {
  for (const auto & file: CF_EXPECT(DirectoryContents(image_dir))) {
    if (android::base::EndsWith(file, ".img")) {
      return image_dir;
    }
  }

  std::string subdir = image_dir + _TARGET_FILES_IMAGES_DIR_NAME;
  for (const auto & file: CF_EXPECT(DirectoryContents(subdir))) {
    if (android::base::EndsWith(file, ".img")) {
      return subdir;
    }
  }
  return CF_ERR("Cannot find images in " << image_dir);
}

/*
 * Map a partition name to an image path.
 *
 * This function is used with BuildSuperImage to mix
 * image_dir and image_paths into the output file.
 */
Result<std::string> GetImageForPartition(
    std::string const &partition_name, std::string const &image_dir,
    const std::map<std::string, std::string>& image_paths) {
  std::string result_path = "";
  if (auto search = image_paths.find(partition_name);
      search != image_paths.end()) {
    result_path = search->second;
  }
  if (result_path == "") {
    result_path = image_dir + partition_name + ".img";
  }

  CF_EXPECT(FileExists(result_path),
            "Cannot find image for partition " << partition_name);
  return result_path;
}

/*
 * Rewrite lpmake and image paths in misc_info.txt.
 */
Result<void> _RewriteMiscInfo(
    const std::string& output_file, const std::string& input_file,
    const std::string& lpmake_path,
    const std::function<Result<std::string>(const std::string&)>& get_image) {
  std::vector<std::string> partition_names;
  std::ifstream input_fs;
  std::ofstream output_fs;
  input_fs.open(input_file);
  output_fs.open(output_file);
  CF_EXPECT(output_fs.is_open(), "Failed to open file: " << output_file);
  std::string line;
  while (getline(input_fs, line)) {
    std::vector<std::string> split_line = android::base::Split(line, "=");
    if (split_line.size() < 2) {
      split_line = { split_line[0], "" };
    }
    if (split_line[0] == "dynamic_partition_list") {
      partition_names = android::base::Tokenize(split_line[1], " ");
    } else if (split_line[0] == "lpmake") {
      output_fs << "lpmake=" << lpmake_path << "\n";
      continue;
    } else if (android::base::EndsWith(split_line[0], "_image")) {
      continue;
    }
    output_fs << line << "\n";
  }
  input_fs.close();

  if (partition_names.size() == 0) {
    LOG(INFO) << "No dynamic partition list in misc info.";
  }

  for (const auto & partition_name : partition_names) {
    output_fs << partition_name << "_image=" <<
        CF_EXPECT(get_image(partition_name)) << "\n";
  }

  output_fs.close();
  return {};
}

class AcloudMixSuperImageCommand : public CvdServerHandler {
 public:
  AcloudMixSuperImageCommand() {}
  ~AcloudMixSuperImageCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    if (invocation.arguments.size() >= 2) {
      if (invocation.command == "acloud" &&
          invocation.arguments[0] == "mix-super-image") {
        return true;
      }
    }
    return false;
  }

  cvd_common::Args CmdList() const override { return {}; }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    auto invocation = ParseInvocation(request.Message());
    if (invocation.arguments.empty() || invocation.arguments.size() < 2) {
      return CF_ERR("Acloud mix-super-image command not support");
    }

    // cvd acloud mix-super-image --super_image path
    cvd::Response response;
    response.mutable_command_response();
    bool help = false;
    std::string flag_paths = "";
    std::vector<Flag> mixsuperimage_flags = {
        GflagsCompatFlag("help", help),
        GflagsCompatFlag("super_image", flag_paths),
    };
    CF_EXPECT(ConsumeFlags(mixsuperimage_flags, invocation.arguments),
              "Failed to process mix-super-image flag.");
    if (help) {
      WriteAll(request.Out(), kMixSuperImageHelpMessage);
      return response;
    }

    auto cb_unlock = [&interrupt_lock](void) -> Result<void> {
      interrupt_lock.unlock();
      return {};
    };
    CF_EXPECT(MixSuperImage(flag_paths , cb_unlock),
              "Build mixed super image failed");
    return response;
  }
  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    CF_EXPECT(waiter_.Interrupt());
    return {};
  }

 private:
  /*
   * Use build_super_image to create a super image.
   */
  Result<void> BuildSuperImage(
      const std::string& output_path, const std::string& misc_info_path,
      const std::function<Result<void>(void)>& callback_unlock,
      const std::function<Result<std::string>(const std::string&)>& get_image) {
    std::string build_super_image_binary;
    std::string lpmake_binary;
    std::string otatools_path;
    if (FileExists(DefaultHostArtifactsPath("otatools/bin/build_super_image"))) {
      build_super_image_binary =
          DefaultHostArtifactsPath("otatools/bin/build_super_image");
      lpmake_binary =
          DefaultHostArtifactsPath("otatools/bin/lpmake");
      otatools_path = DefaultHostArtifactsPath("otatools");
    } else if (FileExists(HostBinaryPath("build_super_image"))) {
      build_super_image_binary =
          HostBinaryPath("build_super_image");
      lpmake_binary =
          HostBinaryPath("lpmake");
      otatools_path = DefaultHostArtifactsPath("");
    } else {
      return CF_ERR("Could not find otatools");
    }

    TemporaryFile new_misc_info;
    std::string new_misc_info_path = new_misc_info.path;
    CF_EXPECT(_RewriteMiscInfo(new_misc_info_path, misc_info_path,
                               lpmake_binary, get_image));

    Command command(build_super_image_binary);
    command.AddParameter(new_misc_info_path);
    command.AddParameter(output_path);
    auto subprocess = command.Start();
    CF_EXPECT(subprocess.Started());
    CF_EXPECT(waiter_.Setup(std::move(subprocess)));
    CF_EXPECT(callback_unlock());
    CF_EXPECT(waiter_.Wait());
    return {};
  }

  Result<void> MixSuperImage(
      const std::string& paths,
      const std::function<Result<void>(void)>& callback_unlock) {
    std::string super_image = "";
    std::string local_system_image = "";
    std::string system_image_path = "";
    std::string image_dir = "";
    std::string misc_info = "";

    int index = 0;
    std::vector<std::string> paths_vec = android::base::Split(paths, ",");
    for (const auto & each_path :paths_vec) {
      if (index == 0) {
        super_image = each_path;
      } else if (index == 1) {
        local_system_image = each_path;
      } else if (index == 2) {
        image_dir = each_path;
      }
      index++;
    }
    // no specific image directory, use $ANDROID_PRODUCT_OUT
    if (image_dir == "") {
      image_dir = DefaultGuestImagePath("/");
    }
    if (!android::base::EndsWith(image_dir, "/")) {
      image_dir += "/";
    }
    misc_info = CF_EXPECT(FindMiscInfo(image_dir));
    image_dir = CF_EXPECT(FindImageDir(image_dir));
    system_image_path = FindImage(local_system_image, {_SYSTEM_IMAGE_NAME_PATTERN});
    CF_EXPECT(system_image_path != "",
              "Cannot find system.img in " << local_system_image);
    std::string system_ext_image_path =
        FindImage(local_system_image, {_SYSTEM_EXT_IMAGE_NAME_PATTERN});
    std::string product_image_path =
        FindImage(local_system_image, {_PRODUCT_IMAGE_NAME_PATTERN});

    return BuildSuperImage(
        super_image, misc_info, callback_unlock,
        [&](const std::string& partition) -> Result<std::string> {
          return GetImageForPartition(partition, image_dir,
                                      {
                                          {"system", system_image_path},
                                          {"system_ext", system_ext_image_path},
                                          {"product", product_image_path},
                                      });
        });
  }

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
  SubprocessWaiter waiter_;
};

std::unique_ptr<CvdServerHandler> NewAcloudMixSuperImageCommand() {
  return std::unique_ptr<CvdServerHandler>(new AcloudMixSuperImageCommand());
}

}  // namespace cuttlefish
