#include "cuttlefish/host/commands/cvd/cli/commands/logs.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/common/libs/utils/files.h"

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
        GflagsCompatFlag("print", print_target),
        GflagsCompatFlag("p", print_target),
    };
  }
};

Result<void> PrintLogsList(const std::string& dir) {
  auto callback = [](const std::string& filename) -> Result<void> {
    constexpr int kMaxPadding = 30;
    std::string basename = android::base::Basename(filename);
    std::cout << basename;
    std::cout << std::string(std::max(int(kMaxPadding - basename.length()), 1), ' ');
    std::cout << filename;
    std::cout << std::endl;
    return {};
  };
  return WalkDirectory(dir, callback);
}

Result<void> PrintLog(const std::string& filename) {
  Command cat("cat");
  cat.AddParameter(filename);
  int exit_code = cat.Start().Wait();
  CF_EXPECTF(exit_code == 0, "{} {} failed: exited with status {}",
             cat.Executable(), filename, exit_code);
  return {};
}

class CvdLogsHandler : public CvdCommandHandler {
 public:
  CvdLogsHandler(InstanceManager& instance_manager) : instance_manager_(instance_manager) {}

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
    CF_EXPECT(FileExists(logs_dir));
    CF_EXPECT(IsDirectory(logs_dir));

    if (opts.print_target.empty()) {
      return PrintLogsList(logs_dir);
    }

    std::string print_target = logs_dir + "/" + opts.print_target;
    CF_EXPECT(FileExists(print_target));
    return PrintLog(print_target);
  }

  cvd_common::Args CmdList() const override { return {"logs"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  bool RequiresDeviceExists() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler(InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(new CvdLogsHandler(instance_manager));
}

}  // namespace cuttlefish
