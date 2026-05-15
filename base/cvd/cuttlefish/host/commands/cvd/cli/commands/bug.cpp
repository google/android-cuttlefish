/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/bug.h"

#include <fcntl.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/bugreport.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/log_files.h"
#include "cuttlefish/host/commands/cvd/cli/log_tail.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/version/version.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "File an issue using go/bugged";
constexpr char kHelpMessage[] = R"(
usage: cvd bug

  `cvd bug` will invoke `bugged create` to file an issue against Cuttlefish.
  It requires `bugged` to be installed on the system.
  See go/bugged for more information.
)";

Result<std::string> ParseBugId(std::string_view stdout_str) {
  const std::string_view prefix = "http://b/";
  const size_t pos = stdout_str.find(prefix);
  CF_EXPECT(pos != std::string_view::npos, "Prefix not found");

  const size_t start = pos + prefix.size();
  size_t end = start;
  while (end < stdout_str.size() && std::isdigit(stdout_str[end])) {
    end++;
  }
  CF_EXPECT(end > start, "No digits found after prefix");

  return std::string(stdout_str.substr(start, end - start));
}

Result<LocalInstance> GetLatestLocalInstance(
    const InstanceDatabase& instance_db) {
  const std::vector<LocalInstanceGroup> groups =
      CF_EXPECT(instance_db.InstanceGroups());
  CF_EXPECT(!groups.empty(), "No instance groups found.");

  const auto latest_group = std::max_element(
      groups.begin(), groups.end(),
      [](const LocalInstanceGroup& a, const LocalInstanceGroup& b) {
        return a.StartTime() < b.StartTime();
      });

  CF_EXPECT(!latest_group->Instances().empty(),
            "Latest instance group has no instances.");
  return latest_group->Instances().front();
}

Result<std::string> GenerateIssueText() {
  const std::optional<std::string> previous_log_opt =
      CF_EXPECT(GetPreviousLogFile());
  CF_EXPECT(previous_log_opt.has_value(), "No previous log file found.");
  const std::string previous_log = *previous_log_opt;

  const SharedFD fd = SharedFD::Open(previous_log, O_RDONLY);
  CF_EXPECTF(fd->IsOpen(), "Failed to open log file {}: {}", previous_log,
             fd->StrError());

  const std::vector<std::string> lines = CF_EXPECTF(
      GetLastNLines(fd, 30), "Failed to read log file {}", previous_log);
  const std::string log_tail =
      fmt::format("```\n{}\n```\n", fmt::join(lines, "\n"));

  const std::string username = CF_EXPECT(CurrentUserName());

  return fmt::format(
      "Cuttlefish bug report\n\n"
      "{}\n"
      "CVD Version:\n{}\n\n"
      "CC+=cloud-android-devs\n"
      "COMPONENT=162041\n"
      "HOTLIST+=1883485\n"
      "PRIORITY=P2\n"
      "REPORTER={}\n"
      "SEVERITY=S2\n"
      "STATUS=NEW\n"
      "TYPE=BUG\n",
      log_tail, GetVersionIds().ToPrettyString(), username);
}

Result<std::string> GetBuggedBinary() {
  CF_EXPECT(FileExists("/usr/bin/gcertstatus"), "Not a Googler desktop.");
  const int gcert_status = Execute({"/usr/bin/gcertstatus"});
  CF_EXPECT(gcert_status == 0, "Please run gcert.");

  if (FileExists("/usr/bin/bugged")) {
    return "/usr/bin/bugged";
  }
  return "/google/bin/releases/bugged/bugged";
}

Result<void> ProduceBugreport(const InstanceDatabase& instance_db,
                              const cvd_common::Envs& env) {
  const LocalInstance latest_instance =
      CF_EXPECT(GetLatestLocalInstance(instance_db));
  const std::string android_host_out = latest_instance.host_artifacts_path();
  const std::string home = latest_instance.home_directory();
  const std::string log_dir = CvdUserLogDir();

  CF_EXPECT(RunHostBugreportCommand(android_host_out, home, env, {}, log_dir));
  return {};
}

Result<std::string> FileIssue(const std::string& bugged_bin,
                              const std::string& issue_text) {
  Command command(bugged_bin);
  command.AddParameter("create");
  command.AddParameter("--format=MARKDOWN");

  std::string stdout_str;
  const int exit_code = RunWithManagedStdio(std::move(command), &issue_text,
                                            &stdout_str, nullptr);
  CF_EXPECTF(exit_code == 0, "bugged exited with code {}", exit_code);

  const std::string bug_id =
      CF_EXPECTF(ParseBugId(stdout_str),
                 "Failed to parse bug ID from bugged output: {}", stdout_str);
  return bug_id;
}

Result<void> AttachFile(const std::string& bugged_bin,
                        const std::string& bug_id,
                        const std::string& file_path) {
  if (FileExists(file_path)) {
    Command attach_cmd(bugged_bin);
    attach_cmd.AddParameter("attach");
    attach_cmd.AddParameter(bug_id);
    attach_cmd.AddParameter(file_path);

    const int attach_status =
        RunWithManagedStdio(std::move(attach_cmd), nullptr, nullptr, nullptr);
    if (attach_status != 0) {
      std::cerr << "Failed to attach file " << file_path << " to bug " << bug_id
                << std::endl;
    }
  } else {
    std::cerr << "File " << file_path << " does not exist to attach."
              << std::endl;
  }
  return {};
}

}  // namespace

CvdBugCommandHandler::CvdBugCommandHandler(const InstanceDatabase& instance_db)
    : instance_db_(instance_db) {}

Result<void> CvdBugCommandHandler::Handle(const CommandRequest& request) {
  const std::string bugged_bin = CF_EXPECT(GetBuggedBinary());
  const std::string issue_text = CF_EXPECT(GenerateIssueText());
  CF_EXPECT(ProduceBugreport(instance_db_, request.Env()));
  const std::string bug_id = CF_EXPECT(FileIssue(bugged_bin, issue_text));
  std::cout << "Created issue http://b/" << bug_id << std::endl;

  const std::string bugreport_zip = CvdUserLogDir() + "/host_bugreport.zip";
  CF_EXPECT(AttachFile(bugged_bin, bug_id, bugreport_zip));

  const std::optional<std::string> previous_log_opt =
      CF_EXPECT(GetPreviousLogFile());
  if (previous_log_opt) {
    CF_EXPECT(AttachFile(bugged_bin, bug_id, *previous_log_opt));
  } else {
    std::cerr << "No previous log file found to attach." << std::endl;
  }

  return {};
}

std::vector<std::string> CvdBugCommandHandler::CmdList() const {
  return {"bug"};
}

std::string CvdBugCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdBugCommandHandler::DetailedHelp(const CommandRequest&) {
  return kHelpMessage;
}

std::unique_ptr<CvdCommandHandler> NewCvdBugCommandHandler(
    const InstanceDatabase& instance_db) {
  return std::make_unique<CvdBugCommandHandler>(instance_db);
}

}  // namespace cuttlefish
