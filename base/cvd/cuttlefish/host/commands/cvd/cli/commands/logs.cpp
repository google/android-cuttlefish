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

#include "cuttlefish/ansi_codes/ansi_codes.h"
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

Result<void> PrintLogsList(
    const std::vector<std::pair<LocalInstanceGroup,
                                std::vector<LocalInstance>>>& found_instances) {
  for (const auto& [group, instances] : found_instances) {
    const std::vector<std::string> group_logs =
        RemoveInaccessibleFilenames(group.LogsFilenames());
    for (const std::string& filename : group_logs) {
      std::cout << group.GroupName() << ":" << android::base::Basename(filename)
                << " " << filename << std::endl;
    }
    for (const LocalInstance& instance : instances) {
      std::vector<std::string> ins_logs =
          RemoveInaccessibleFilenames(CF_EXPECT(instance.LogsFilenames()));
      for (const std::string& filename : ins_logs) {
        std::cout << group.GroupName() << ":" << instance.Name() << ":"
                  << android::base::Basename(filename) << " " << filename
                  << std::endl;
      }
    }
  }
  return {};
}

Result<void> PrintLog(const std::string& filename) {
  const char* exec_name = isatty(STDOUT_FILENO) ? "less" : "cat";
  execlp(exec_name, exec_name, filename.c_str(), nullptr);
  return CF_ERR("execlp failed: " << strerror(errno));
}

Result<void> PrintLogsTree(
    const std::vector<std::pair<LocalInstanceGroup,
                                std::vector<LocalInstance>>>& found_instances) {
  for (const auto& [group, instances] : found_instances) {
    std::cout << kAnsiBoldCyan << group.GroupName() << kAnsiReset << std::endl;

    const std::vector<std::string> group_logs =
        RemoveInaccessibleFilenames(group.LogsFilenames());

    struct InstanceWithLogs {
      LocalInstance instance;
      std::vector<std::string> logs;
    };
    std::vector<InstanceWithLogs> instances_with_logs;
    instances_with_logs.reserve(instances.size());
    for (const LocalInstance& instance : instances) {
      std::vector<std::string> ins_logs =
          RemoveInaccessibleFilenames(CF_EXPECT(instance.LogsFilenames()));
      instances_with_logs.push_back({instance, std::move(ins_logs)});
    }

    int max_visible_width = 0;
    for (const std::string& log : group_logs) {
      const std::string basename = android::base::Basename(log);
      max_visible_width =
          std::max(max_visible_width, 4 + (int)basename.length());
    }
    for (const InstanceWithLogs& inst : instances_with_logs) {
      for (const std::string& log : inst.logs) {
        const std::string basename = android::base::Basename(log);
        max_visible_width =
            std::max(max_visible_width, 8 + (int)basename.length());
      }
    }
    const int target_column = std::min(max_visible_width + 4, 30);

    const size_t total_children =
        group_logs.size() + instances_with_logs.size();
    size_t child_idx = 0;

    for (size_t i = 0; i < group_logs.size(); ++i, ++child_idx) {
      const bool is_last_child = (child_idx == total_children - 1);
      const std::string branch = is_last_child ? "└── " : "├── ";
      const std::string basename = android::base::Basename(group_logs[i]);

      std::cout << branch;
      std::cout << basename;

      const int visible_length = 4 + basename.length();
      const int padding = std::max(target_column - visible_length, 1);
      std::cout << std::string(padding, ' ');
      std::cout << kAnsiGrey << group_logs[i] << kAnsiReset << std::endl;
    }

    for (size_t j = 0; j < instances_with_logs.size(); ++j, ++child_idx) {
      const bool is_last_child = (child_idx == total_children - 1);
      const std::string branch = is_last_child ? "└── " : "├── ";

      std::cout << branch;
      std::cout << kAnsiBoldBlue << instances_with_logs[j].instance.Name()
                << kAnsiReset << std::endl;

      const std::string inst_prefix = is_last_child ? "    " : "│   ";
      const std::vector<std::string>& ins_logs = instances_with_logs[j].logs;

      for (size_t k = 0; k < ins_logs.size(); ++k) {
        const bool is_last_log = (k == ins_logs.size() - 1);
        const std::string leaf_branch = is_last_log ? "└── " : "├── ";
        const std::string basename = android::base::Basename(ins_logs[k]);

        std::cout << inst_prefix << leaf_branch;
        std::cout << basename;

        const int visible_length = 8 + basename.length();
        const int padding = std::max(target_column - visible_length, 1);
        std::cout << std::string(padding, ' ');
        std::cout << kAnsiGrey << ins_logs[k] << kAnsiReset << std::endl;
      }
    }
  }
  return {};
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

  if (pretty_) {
    CF_EXPECT(PrintLogsTree(found_instances));
  } else {
    CF_EXPECT(PrintLogsList(found_instances));
  }

  return {};
}

cvd_common::Args CvdLogsHandler::CmdList() const { return {"logs"}; }

std::string CvdLogsHandler::SummaryHelp() const { return kSummaryHelpText; }

bool CvdLogsHandler::RequiresDeviceExists() const { return true; }

std::vector<HelpParagraph> CvdLogsHandler::Description() const {
  return {
      HelpParagraph("The `logs` command lists or displays the contents of log "
                    "files generated by Cuttlefish instances and groups."),

      HelpParagraph(
          "By default, it prints a pretty tree-structured representation of "
          "the log files if stdout is a TTY. If stdout is not a TTY (e.g. when "
          "piping to another command) or if `--nopretty` is used, it prints a "
          "flat list of logs in the format:"),

      HelpParagraph::Raw(
          "  <group_name>:<basename> <full_path>            (for group-level "
          "logs)\n  <group_name>:<instance_name>:<basename> <path>  (for "
          "instance-level logs)"),

      HelpParagraph::Raw(
          R"(Common Log Files:
  Group-level logs:
    assemble_cvd.log       Logs from the assembly phase (setting up disks,
                           configs, etc.).
    fetch.log              Logs from fetching build artifacts.
    metrics.log            Logs from metrics collection.
    cuttlefish_config.json The generated cuttlefish configuration.)"),

      HelpParagraph::Raw(
          R"(  Instance-level logs:
    launcher.log           Logs from the Cuttlefish runner/launcher (run_cvd).
    kernel.log             Kernel logs from the Android guest VM.
    logcat                 Android guest logcat output.
    crosvm_openwrt.log     Logs from the OpenWrt VM (for networking).
    modem_simulator.log    Logs from the modem simulator (if enabled).)"),

      HelpParagraph::Raw(
          R"(Examples:
  List all logs for all instances in a tree structure:
    $ cvd logs)"),

      HelpParagraph::Raw(
          R"(  List all logs in a flat format (suitable for scripting):
    $ cvd logs --nopretty
    $ cvd logs | cat)"),

      HelpParagraph::Raw(
          R"(  Print the launcher.log file for the default instance:
    $ cvd logs -p launcher.log)"),

      HelpParagraph::Raw(
          R"(  Print the kernel.log for a specific instance 'cvd-2' in group 'mygroup':
    $ cvd -group_name mygroup -instance_name cvd-2 logs -p kernel.log)"),
  };
}

Result<std::vector<Flag>> CvdLogsHandler::Flags(const CommandRequest&) {
  return std::vector<Flag>{
      GflagsCompatFlag("print", print_target_flag_)
          .ValueNameHint("LOGFILE")
          .Alias("p")
          .Help("Log file name to print. Only the base name of the file is "
                "needed, ex: 'launcher.log'."),
      GflagsCompatFlag("pretty", pretty_)
          .Help("Stylize output for human readability. The default when output "
                "is a terminal."),
  };
}

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdLogsHandler(instance_manager));
}

}  // namespace cuttlefish
