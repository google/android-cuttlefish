#pragma once

#include <memory>
#include <string>

#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class CvdLogsHandler : public CvdCommandHandler {
 public:
  CvdLogsHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override;

  std::string SummaryHelp() const override;
  bool RequiresDeviceExists() const override;
  std::vector<HelpParagraph> Description() const override;
  Result<std::vector<Flag>> Flags(const CommandRequest& request) override;

 private:
  Result<void> HandlePrint(const CommandRequest& request);
  Result<void> HandleList(const CommandRequest& request);

  InstanceManager& instance_manager_;
  std::string print_target_flag_;
  bool pretty_;
};

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler(
    InstanceManager& instance_manager);

}  // namespace cuttlefish
