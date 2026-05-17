#include "cuttlefish/host/commands/cvd/cli/commands/logs.h"

#include <android-base/file.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Print device logs";

constexpr char kDetailedHelpText[] = R"(
usage: cvd [--group_name name [--instance_name name]] logs [-p|--print name]
)";

struct LogsCmdOptions {
  std::string print_target;

  std::vector<Flag> Flags() {
    return {
        GflagsCompatFlag("print", print_target)
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "-p"}),
        UnexpectedArgumentGuard(),
    };
  }
};

void PrintLogsList(const std::vector<std::string>& filenames) {
  for (auto& filename : filenames) {
    std::string basename = android::base::Basename(filename);
    std::cout << basename;
    std::cout << " ";
    if (isatty(STDOUT_FILENO)) {
      constexpr int kMaxPadding = 30;
      // Add more spaces for a clear two column view printing to a terminal.
      std::cout << std::string(
          std::max(int(kMaxPadding - basename.length()), 1), ' ');
    }
    std::cout << filename;
    std::cout << std::endl;
  };
}

std::vector<std::string> RemoveInaccessibleFilenames(
    std::vector<std::string> filenames) {
  auto removed_begin = std::remove_if(
      filenames.begin(), filenames.end(), [](const std::string& v) {
        bool accessible = access(v.c_str(), F_OK) == 0;
        if (!accessible) {
          std::string basename = android::base::Basename(v);
          VLOG(0) << "Logs file `" << basename << "` not found at \"" << v
                  << "\"";
        }
        return !accessible;
      });
  filenames.erase(removed_begin, filenames.end());
  return filenames;
}

Result<void> PrintLog(const std::string& filename) {
  const char* exec_name = isatty(STDOUT_FILENO) ? "less" : "cat";
  execlp(exec_name, exec_name, filename.c_str(), nullptr);
  return CF_ERR("execlp failed: " << strerror(errno));
}

}  // namespace

CvdLogsHandler::CvdLogsHandler(InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> CvdLogsHandler::Handle(const CommandRequest& request) {
  auto [instance, group] =
      CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                "Unable to select an instance");

  LogsCmdOptions opts;
  std::vector<std::string> args = request.SubcommandArguments();
  CF_EXPECT(ConsumeFlags(opts.Flags(), args));

  std::vector<std::string> logs_filenames;
  auto group_names = RemoveInaccessibleFilenames(group.LogsFilenames());
  logs_filenames.insert(logs_filenames.end(), group_names.begin(),
                        group_names.end());
  auto ins_names =
      RemoveInaccessibleFilenames(CF_EXPECT(instance.LogsFilenames()));
  // Avoid inserting instance log names that are already fetched at group level.
  for (auto ins_filename : ins_names) {
    std::string base = android::base::Basename(ins_filename);
    auto p = [base](const auto& v) {
      return base == android::base::Basename(v);
    };
    auto it = std::find_if(group_names.begin(), group_names.end(), p);
    if (it == group_names.end()) {
      logs_filenames.push_back(ins_filename);
    }
  }

  if (opts.print_target.empty()) {
    if (logs_filenames.empty()) {
      LOG(INFO) << "There are no log files available";
    } else {
      PrintLogsList(logs_filenames);
    }
    return {};
  }

  for (auto& filename : logs_filenames) {
    std::string basename = android::base::Basename(filename);
    if (basename == opts.print_target) {
      CF_EXPECT(PrintLog(filename));
      return {};
    }
  };

  CF_EXPECTF(false, "Not found `{}` logs", opts.print_target);

  return {};
}

cvd_common::Args CvdLogsHandler::CmdList() const { return {"logs"}; }

std::string CvdLogsHandler::SummaryHelp() const { return kSummaryHelpText; }

bool CvdLogsHandler::RequiresDeviceExists() const { return true; }

Result<std::string> CvdLogsHandler::DetailedHelp(const CommandRequest&) const {
  return kDetailedHelpText;
}

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdLogsHandler(instance_manager));
}

}  // namespace cuttlefish
