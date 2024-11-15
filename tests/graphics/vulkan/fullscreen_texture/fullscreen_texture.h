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

class FullscreenTexture : public SampleBase {
 public:
  static Result<std::unique_ptr<SampleBase>> Create();

  Result<Ok> StartUp() override;
  Result<Ok> CleanUp() override;

  Result<Ok> CreateSwapchainDependents(const SwapchainInfo& /*info*/) override;
  Result<Ok> DestroySwapchainDependents() override;

  Result<Ok> RecordFrame(const FrameInfo& frame) override;

 private:
  FullscreenTexture() = default;

  vkhpp::UniqueRenderPass mRenderpass;
  struct SwapchainImageObjects {
    vkhpp::Extent2D extent;
    vkhpp::UniqueFramebuffer framebuffer;
  };
  std::vector<SwapchainImageObjects> mSwapchainImageObjects;

  ImageWithMemory mTexture;
  vkhpp::UniqueSampler mTextureSampler;
  vkhpp::UniqueShaderModule mVertShaderModule;
  vkhpp::UniqueShaderModule mFragShaderModule;
  vkhpp::UniquePipelineLayout mPipelineLayout;
  vkhpp::UniqueDescriptorSetLayout mDescriptorSet0Layout;
  vkhpp::UniqueDescriptorPool mDescriptorSet0Pool;
  vkhpp::UniqueDescriptorSet mDescriptorSet0;
  vkhpp::UniquePipeline mPipeline;
};

}  // namespace cuttlefish