#pragma once

#include <optional>
#include <string>

#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class CvdLogsHandler : public CvdCommandHandler {
 public:
  CvdLogsHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  std::vector<std::string> CmdList() const override;

  std::string SummaryHelp() const override;
  bool RequiresDeviceExists() const override;
  std::vector<HelpParagraph> Description() const override;
  Result<std::vector<Flag>> Flags(const CommandRequest& request) override;

 private:
  Result<void> HandlePrint(const CommandRequest& request);
  Result<void> HandleList(const CommandRequest& request);

  InstanceManager& instance_manager_;
  std::optional<std::string> print_target_flag_;
  bool pretty_;
  bool pager_;
};

}  // namespace cuttlefish
