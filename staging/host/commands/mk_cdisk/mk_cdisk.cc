//
// Copyright (C) 2021 The Android Open Source Project
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

#include <fstream>
#include <iostream>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/result.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "host/libs/image_aggregator/image_aggregator.h"

using android::base::ErrnoError;
using android::base::Error;
using android::base::Result;

using cuttlefish::CreateCompositeDisk;
using cuttlefish::FileExists;
using cuttlefish::ImagePartition;
using cuttlefish::kLinuxFilesystem;

// Returns `append` is appended to the end of filename preserving the extension.
std::string AppendFileName(const std::string& filename,
                           const std::string& append) {
  size_t pos = filename.find_last_of('.');
  if (pos == std::string::npos) {
    return filename + append;
  } else {
    return filename.substr(0, pos) + append + filename.substr(pos);
  }
}

// config JSON schema:
// {
//   "partitions": [
//     {
//       "label": string,
//       "path": string,
//       "writable": bool, // optional. defaults to false.
//     }
//   ]
// }

Result<std::vector<ImagePartition>> LoadConfig(std::istream& in) {
  std::vector<ImagePartition> partitions;

  Json::CharReaderBuilder builder;
  Json::Value root;
  Json::String errs;
  if (!parseFromStream(builder, in, &root, &errs)) {
    return Error() << "bad config: " << errs;
  }
  for (const Json::Value& part : root["partitions"]) {
    const std::string label = part["label"].asString();
    const std::string path = part["path"].asString();
    const bool writable =
        part["writable"].asBool();  // default: false (if null)

    if (!FileExists(path)) {
      return Error() << "bad config: Can't find \'" << path << '\'';
    }
    partitions.push_back(
        ImagePartition{label, path, kLinuxFilesystem, .read_only = !writable});
  }

  if (partitions.empty()) {
    return Error() << "bad config: no partitions";
  }
  return partitions;
}

Result<std::vector<ImagePartition>> LoadConfig(const std::string& config_file) {
  if (config_file == "-") {
    return LoadConfig(std::cin);
  } else {
    std::ifstream in(config_file);
    if (!in) {
      return ErrnoError() << "Can't open file \'" << config_file << '\'';
    }
    return LoadConfig(in);
  }
}

struct CompositeDiskArgs {
  std::string config_file;
  std::string output_file;
};

Result<CompositeDiskArgs> ParseCompositeDiskArgs(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << fmt::format(
        "Usage: {0} <config_file> <output_file>\n"
        "   or  {0} - <output_file>  (read config from STDIN)\n",
        argv[0]);
    return Error() << "missing arguments.";
  }
  CompositeDiskArgs args{
      .config_file = argv[1],
      .output_file = argv[2],
  };
  return args;
}

Result<void> MakeCompositeDiskMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  auto args = ParseCompositeDiskArgs(argc, argv);
  if (!args.ok()) {
    return args.error();
  }
  auto partitions = LoadConfig(args->config_file);
  if (!partitions.ok()) {
    return partitions.error();
  }

  // We need two implicit output paths: GPT header/footer
  // e.g. out.img will have out-header.img and out-footer.img
  std::string gpt_header = AppendFileName(args->output_file, "-header");
  std::string gpt_footer = AppendFileName(args->output_file, "-footer");
  CreateCompositeDisk(*partitions, gpt_header, gpt_footer, args->output_file);
  return {};
}

int main(int argc, char** argv) {
  auto result = MakeCompositeDiskMain(argc, argv);
  if (!result.ok()) {
    LOG(ERROR) << result.error();
    return EXIT_FAILURE;
  }
  return 0;
}
