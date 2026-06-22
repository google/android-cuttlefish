#include "cuttlefish/host/commands/cvd/cli/commands/logs.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
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
                   const std::vector<std::string>& filenames) {
  for (auto& filename : filenames) {
    std::string basename = android::base::Basename(filename);
    std::string prefix = group_name + ":" + instance_name + ":" + basename;
    std::cout << prefix;
    std::cout << " ";
    if (isatty(STDOUT_FILENO)) {
      constexpr int kMaxPadding = 30;
      // Add more spaces for a clear two column view printing to a terminal.
      std::cout << std::string(std::max(int(kMaxPadding - prefix.length()), 1),
                               ' ');
    }
    std::cout << filename;
    std::cout << std::endl;
  };
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

Result<std::vector<std::string>> GetInstanceLogs(
    const LocalInstance& instance, const LocalInstanceGroup& group) {
  std::vector<std::string> logs_filenames;
  auto group_names = RemoveInaccessibleFilenames(group.LogsFilenames());
  logs_filenames = group_names;
  auto ins_names =
      RemoveInaccessibleFilenames(CF_EXPECT(instance.LogsFilenames()));
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
  return logs_filenames;
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
  std::vector<std::string> args = request.SubcommandArguments();
  auto flags = CF_EXPECT(Flags(request));
  CF_EXPECT(ConsumeFlags(flags, args, {.fail_on_unexpected_argument = true}));

  if (!print_target_flag_.empty()) {
    auto [instance, group] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");
    auto logs_filenames = CF_EXPECT(GetInstanceLogs(instance, group));
    for (auto& filename : logs_filenames) {
      std::string basename = android::base::Basename(filename);
      if (basename == print_target_flag_) {
        CF_EXPECT(PrintLog(filename));
        return {};
      }
    }
    return CF_ERRF("Not found `{}` logs", print_target_flag_);
  }

  // Listing mode
  if (request.Selectors().instance_names) {
    // If instance_names is specified, we must resolve to a single instance.
    auto [instance, group] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");
    auto logs_filenames = CF_EXPECT(GetInstanceLogs(instance, group));
    if (logs_filenames.empty()) {
      LOG(INFO) << "There are no log files available";
    } else {
      PrintLogsList(group.GroupName(), instance.Name(), logs_filenames);
    }
    return {};
  }

  // Otherwise, list all matching instances.
  auto found_instances =
      CF_EXPECT(selector::SelectInstances(instance_manager_, request));

  if (found_instances.empty()) {
    LOG(INFO) << "There are no log files available";
    return {};
  }

  for (const auto& [group, instances] : found_instances) {
    for (const auto& instance : instances) {
      auto logs_filenames = CF_EXPECT(GetInstanceLogs(instance, group));
      PrintLogsList(group.GroupName(), instance.Name(), logs_filenames);
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
  };
}

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdLogsHandler(instance_manager));
}

}  // namespace cuttlefish
