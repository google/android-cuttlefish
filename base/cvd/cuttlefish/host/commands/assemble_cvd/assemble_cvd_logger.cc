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

#include <fcntl.h>
#include <sys/stat.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

SharedFD AssembleCvdLogger(std::string runtime_dir_parent) {
  Result<SharedFD> log_file;
  if (InSandbox()) {
    log_file =
        Fd::Open(absl::StrCat(runtime_dir_parent, "/instances/cvd-1/logs/",
                              kLogNameLauncher),
                 O_WRONLY | O_APPEND);
  } else {
    while (runtime_dir_parent[runtime_dir_parent.size() - 1] == '/') {
      runtime_dir_parent =
          runtime_dir_parent.substr(0, FLAGS_instance_dir.rfind('/'));
    }
    runtime_dir_parent =
        runtime_dir_parent.substr(0, FLAGS_instance_dir.rfind('/'));
    log_file = Fd::Open(runtime_dir_parent, O_WRONLY | O_TMPFILE,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  }
  if (!log_file.has_value()) {
    LOG(ERROR) << "Could not open initial log file: " << log_file.error();
  } else {
    std::vector<SeverityTarget> log_destinations = {
        SeverityTarget::FromFd(SharedFD::Dup(2), MetadataLevel::ONLY_MESSAGE,
                               ConsoleSeverity()),
        SeverityTarget::FromFd(*log_file, MetadataLevel::FULL,
                               LogFileSeverity()),

    };
    SetLoggers(std::move(log_destinations), "");
  }
  return log_file.value_or(Fd());
}

}  // namespace cuttlefish
