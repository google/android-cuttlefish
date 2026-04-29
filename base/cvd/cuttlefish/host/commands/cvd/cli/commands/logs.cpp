#include "cuttlefish/host/commands/cvd/cli/commands/logs.h"

#include <iostream>
#include <memory>
#include <string>

#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] = "Print device logs";

constexpr char kDetailedHelpText[] = R"(
usage: cvd [--group_name name [--instance_name name]] logs
)";

class CvdLogsHandler : public CvdCommandHandler {
 public:
  CvdLogsHandler() = default;

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));
    std::cout << "hello, logs\n";
    return {};
  }

  cvd_common::Args CmdList() const override { return {"logs"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool RequiresDeviceExists() const override { return true; }

  Result<std::string> DetailedHelp(const CommandRequest&) const override {
    return kDetailedHelpText;
  }
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdLogsHandler());
}

}  // namespace cuttlefish
