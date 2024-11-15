// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "common.h"
#include "sample_base.h"

namespace cuttlefish {

class FullscreenColor : public SampleBase {
 public:
  static Result<std::unique_ptr<SampleBase>> Create();

  Result<Ok> StartUp() override;
  Result<Ok> CleanUp() override;

  Result<Ok> CreateSwapchainDependents(const SwapchainInfo& /*info*/) override;
  Result<Ok> DestroySwapchainDependents() override;

  Result<Ok> RecordFrame(const FrameInfo& frame) override;

 private:
  FullscreenColor() = default;

  vkhpp::UniqueRenderPass mRenderpass;
  struct SwapchainImageObjects {
    vkhpp::Extent2D extent;
    vkhpp::UniqueFramebuffer framebuffer;
  };
  std::vector<SwapchainImageObjects> mSwapchainImageObjects;

  vkhpp::UniqueShaderModule mVertShaderModule;
  vkhpp::UniqueShaderModule mFragShaderModule;
  vkhpp::UniquePipelineLayout mPipelineLayout;
  vkhpp::UniquePipeline mPipeline;
};

}  // namespace cuttlefish