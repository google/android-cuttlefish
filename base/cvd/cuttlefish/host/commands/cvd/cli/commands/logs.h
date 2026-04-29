#pragma once

#include <memory>

#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"

namespace cuttlefish {

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler(
    InstanceManager& instance_manager);

}  // namespace cuttlefish
