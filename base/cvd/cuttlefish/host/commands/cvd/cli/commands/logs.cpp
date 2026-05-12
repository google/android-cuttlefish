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
#include "cuttlefish/common/libs/utils/files.h"
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

Result<void> PrintLogsList(const std::string& dir) {
  auto callback = [](const std::string& filename) -> Result<void> {
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
    return {};
  };
  CF_EXPECT(WalkDirectory(dir, callback));
  return {};
}

Result<void> PrintLog(const std::string& filename) {
  const char* exec_name = isatty(STDOUT_FILENO) ? "less" : "cat";
  execlp(exec_name, exec_name, filename.c_str(), nullptr);
  return CF_ERR("execlp failed: " << strerror(errno));
}

class CvdLogsHandler : public CvdCommandHandler {
 public:
  CvdLogsHandler(InstanceManager& instance_manager)
      : instance_manager_(instance_manager) {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    auto [instance, _] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");

    LogsCmdOptions opts;
    std::vector<std::string> args = request.SubcommandArguments();
    CF_EXPECT(ConsumeFlags(opts.Flags(), args));

    std::string dir = instance.instance_dir();
    std::string logs_dir = dir + "/logs";
    if (!FileExists(logs_dir)) {
      VLOG(0) << "Logs directory `" << logs_dir << "` does not exist.";
      LOG(INFO) << "There are no logs files available";
      return {};
    }
    CF_EXPECT(IsDirectory(logs_dir));

    if (opts.print_target.empty()) {
      CF_EXPECT(PrintLogsList(logs_dir));
      return {};
    }

    std::string print_target = logs_dir + "/" + opts.print_target;
    CF_EXPECT(FileExists(print_target));
    CF_EXPECT(PrintLog(print_target));
    return {};
  }

  cvd_common::Args CmdList() const override { return {"logs"}; }

  std::string SummaryHelp() const override { return kSummaryHelpText; }

  bool RequiresDeviceExists() const override { return true; }

  Result<std::string> DetailedHelp(const CommandRequest&) const override {
    return kDetailedHelpText;
  }

 private:
  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdLogsHandler(instance_manager));
}

}  // namespace cuttlefish
