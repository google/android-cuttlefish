#include "cuttlefish/host/commands/cvd/cli/commands/logs.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>

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
usage: cvd [--group_name name [--instance_name name]] logs
)";

class CvdLogsHandler : public CvdCommandHandler {
 public:
  CvdLogsHandler(InstanceManager& instance_manager) : instance_manager_(instance_manager) {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    auto [instance, _] =
        CF_EXPECT(selector::SelectInstance(instance_manager_, request),
                  "Unable to select an instance");

    std::string dir = instance.instance_dir();
    std::string logs_dir = dir + "/logs";

    CF_EXPECT(FileExists(logs_dir));
    CF_EXPECT(IsDirectory(logs_dir));

    auto callback = [](const std::string& filename) -> Result<void> {
      constexpr int kMaxPadding = 30;
      std::string basename = android::base::Basename(filename);
      std::cout << basename;
      std::cout << std::string(std::max(int(kMaxPadding - basename.length()), 1), ' ');
      std::cout << filename;
      std::cout << std::endl;
      return {};
    };

    CF_EXPECT(WalkDirectory(logs_dir, callback));

    return {};
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
