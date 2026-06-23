#include "cuttlefish/host/commands/cvd/cli/commands/logs.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "android-base/file.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "List and display log files";

void PrintLogsList(const std::string& group_name,
                   const std::string& instance_name,
                   const std::vector<std::string>& filenames,
                   const bool pretty) {
  for (const std::string& filename : filenames) {
    std::string basename = android::base::Basename(filename);
    std::string prefix =
        instance_name.empty()
            ? group_name + ":" + basename
            : group_name + ":" + instance_name + ":" + basename;
    std::cout << prefix;
    std::cout << " ";
    if (pretty) {
      constexpr int kMaxPadding = 30;
      // Add more spaces for a clear two column view printing to a terminal.
      std::cout << std::string(std::max(int(kMaxPadding - prefix.length()), 1),
                               ' ');
    }
    std::cout << filename;
    std::cout << std::endl;
  };
}

bool IsGroupLevelLog(const std::string& log_name) {
  std::vector<std::string> basenames = LocalInstanceGroup::GroupLogBasenames();
  return std::find(basenames.begin(), basenames.end(), log_name) !=
         basenames.end();
}

std::vector<std::string> RemoveInaccessibleFilenames(
    std::vector<std::string> filenames) {
  std::erase_if(filenames, [](const std::string& v) {
    bool accessible = access(v.c_str(), F_OK) == 0;
    if (!accessible) {
      std::string basename = android::base::Basename(v);
      VLOG(0) << "Logs file `" << basename << "` not found at \"" << v << "\"";
    }
    return !accessible;
  });
  return filenames;
}

Result<void> PrintLog(const std::string& filename) {
  const char* exec_name = isatty(STDOUT_FILENO) ? "less" : "cat";
  execlp(exec_name, exec_name, filename.c_str(), nullptr);
  return CF_ERR("execlp failed: " << strerror(errno));
}

}  // namespace

CvdLogsHandler::CvdLogsHandler(InstanceManager& instance_manager)
    : instance_manager_(instance_manager), pretty_(isatty(STDOUT_FILENO)) {}

Result<void> CvdLogsHandler::Handle(const CommandRequest& request) {
  std::vector<std::string> args = request.SubcommandArguments();
  std::vector<Flag> flags = CF_EXPECT(Flags(request));
  CF_EXPECT(ConsumeFlags(flags, args, {.fail_on_unexpected_argument = true}));

  if (!print_target_flag_.empty()) {
    CF_EXPECT(HandlePrint(request));
    return {};
  }
  CF_EXPECT(HandleList(request));
  return {};
}

Result<void> CvdLogsHandler::HandlePrint(const CommandRequest& request) {
  if (IsGroupLevelLog(print_target_flag_)) {
    const LocalInstanceGroup group =
        CF_EXPECT(selector::SelectGroup(instance_manager_, request));
    const std::vector<std::string> log_filenames =
        RemoveInaccessibleFilenames(group.LogsFilenames());
    for (const std::string& filename : log_filenames) {
      const std::string basename = android::base::Basename(filename);
      if (basename == print_target_flag_) {
        CF_EXPECT(PrintLog(filename));
        return {};
      }
    }
  } else {
    const auto [instance, group] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");
    const std::vector<std::string> log_filenames =
        RemoveInaccessibleFilenames(CF_EXPECT(instance.LogsFilenames()));
    for (const std::string& filename : log_filenames) {
      const std::string basename = android::base::Basename(filename);
      if (basename == print_target_flag_) {
        CF_EXPECT(PrintLog(filename));
        return {};
      }
    }
  }
  return CF_ERRF("Not found `{}` logs", print_target_flag_);
}

Result<void> CvdLogsHandler::HandleList(const CommandRequest& request) {
  const std::vector<std::pair<LocalInstanceGroup, std::vector<LocalInstance>>>
      found_instances =
          CF_EXPECT(selector::SelectInstances(instance_manager_, request));

  if (found_instances.empty()) {
    LOG(INFO) << "There are no log files available";
    return {};
  }

  for (const auto& [group, instances] : found_instances) {
    const std::vector<std::string> group_logs =
        RemoveInaccessibleFilenames(group.LogsFilenames());
    PrintLogsList(group.GroupName(), "", group_logs, pretty_);
    for (const LocalInstance& instance : instances) {
      std::vector<std::string> ins_logs =
          RemoveInaccessibleFilenames(CF_EXPECT(instance.LogsFilenames()));
      std::erase_if(ins_logs, [](const std::string& path) {
        return IsGroupLevelLog(android::base::Basename(path));
      });
      PrintLogsList(group.GroupName(), instance.Name(), ins_logs, pretty_);
    }
  }

  return {};
}

cvd_common::Args CvdLogsHandler::CmdList() const { return {"logs"}; }

std::string CvdLogsHandler::SummaryHelp() const { return kSummaryHelpText; }

bool CvdLogsHandler::RequiresDeviceExists() const { return true; }

std::vector<HelpParagraph> CvdLogsHandler::Description() const {
  return {
      HelpParagraph("Prints Cuttlefish device logs."),
  };
}

Result<std::vector<Flag>> CvdLogsHandler::Flags(const CommandRequest&) {
  return std::vector<Flag>{
      GflagsCompatFlag("print", print_target_flag_)
          .ValueNameHint("LOGFILE")
          .Alias("p")
          .Help("Log file name to print"),
      GflagsCompatFlag("pretty", pretty_)
          .Help("Stylize output for human readability. The default when "
                "output is a terminal."),
  };
}

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdLogsHandler(instance_manager));
}

}  // namespace cuttlefish
