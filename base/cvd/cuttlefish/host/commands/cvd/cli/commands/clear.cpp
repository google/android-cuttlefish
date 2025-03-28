/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/cvd/cli/commands/clear.h"

#include <sys/types.h>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/scopeguard.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/legacy/cvd_server.pb.h"

namespace cuttlefish {
namespace {

constexpr char kClearCmd[] = "clear";
constexpr char kSummaryHelpText[] =
    "Clears the instance databaase, stopping any running instances first.";

class CvdClearCommandHandler : public CvdCommandHandler {
 public:
  CvdClearCommandHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override { return {kClearCmd}; }
  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }
  bool ShouldInterceptHelp() const override { return true; }
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  InstanceManager& instance_manager_;
};

CvdClearCommandHandler::CvdClearCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<void> CvdClearCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));
  CF_EXPECT_EQ(instance_manager_.CvdClear(request).code(), cvd::Status::OK);
  return {};
}

Result<std::string> CvdClearCommandHandler::DetailedHelp(
    std::vector<std::string>& arguments) const {
  return kSummaryHelpText;
}

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdClearCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdClearCommandHandler(instance_manager));
}

}  // namespace cuttlefish

