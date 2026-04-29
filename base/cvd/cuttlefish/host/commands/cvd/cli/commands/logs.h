#pragma once

#include <memory>

#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"

namespace cuttlefish {

std::unique_ptr<CvdCommandHandler> NewCvdLogsHandler();

}  // namespace cuttlefish
