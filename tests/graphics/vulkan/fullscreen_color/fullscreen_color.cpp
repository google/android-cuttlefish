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

#include "fullscreen_color.h"

namespace cuttlefish {
namespace {

#include "fullscreen_color.frag.inl"
#include "fullscreen_color.vert.inl"

}  // namespace

Result<std::unique_ptr<SampleBase>> BuildVulkanSampleApp() {
  return FullscreenColor::Create();
}

/*static*/
Result<std::unique_ptr<SampleBase>> FullscreenColor::Create() {
  std::unique_ptr<SampleBase> sample(new FullscreenColor());
  VK_EXPECT(sample->StartUp());
  return sample;
}

Result<Ok> FullscreenColor::StartUp() {
  VK_EXPECT(StartUpBase());

  const vkhpp::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
      .setLayoutCount = 0,
  };
  mPipelineLayout = VK_EXPECT_RV(
      mDevice->createPipelineLayoutUnique(pipelineLayoutCreateInfo));

  const vkhpp::ShaderModuleCreateInfo vertShaderCreateInfo = {
      .codeSize = static_cast<uint32_t>(kFullscreenColorVert.size()),
      .pCode = reinterpret_cast<const uint32_t*>(kFullscreenColorVert.data()),
  };
  mVertShaderModule =
      VK_EXPECT_RV(mDevice->createShaderModuleUnique(vertShaderCreateInfo));

  const vkhpp::ShaderModuleCreateInfo fragShaderCreateInfo = {
      .codeSize = static_cast<uint32_t>(kFullscreenColorFrag.size()),
      .pCode = reinterpret_cast<const uint32_t*>(kFullscreenColorFrag.data()),
  };
  mFragShaderModule =
      VK_EXPECT_RV(mDevice->createShaderModuleUnique(fragShaderCreateInfo));

  return Ok{};
}

Result<Ok> FullscreenColor::CleanUp() {
  VK_EXPECT(CleanUpBase());

  mDevice->waitIdle();

  return Ok{};
}

Result<Ok> FullscreenColor::CreateSwapchainDependents(
    const SwapchainInfo& swapchainInfo) {
  const std::vector<vkhpp::AttachmentDescription> renderpassAttachments = {
      {
          .format = swapchainInfo.swapchainFormat,
          .samples = vkhpp::SampleCountFlagBits::e1,
          .loadOp = vkhpp::AttachmentLoadOp::eClear,
          .storeOp = vkhpp::AttachmentStoreOp::eStore,
          .stencilLoadOp = vkhpp::AttachmentLoadOp::eClear,
          .stencilStoreOp = vkhpp::AttachmentStoreOp::eStore,
          .initialLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
          .finalLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
      },
  };
  const vkhpp::AttachmentReference renderpassColorAttachmentRef = {
      .attachment = 0,
      .layout = vkhpp::ImageLayout::eColorAttachmentOptimal,
  };
  const vkhpp::SubpassDescription renderpassSubpass = {
      .pipelineBindPoint = vkhpp::PipelineBindPoint::eGraphics,
      .inputAttachmentCount = 0,
      .pInputAttachments = nullptr,
      .colorAttachmentCount = 1,
      .pColorAttachments = &renderpassColorAttachmentRef,
      .pResolveAttachments = nullptr,
      .pDepthStencilAttachment = nullptr,
      .pPreserveAttachments = nullptr,
  };
  const vkhpp::SubpassDependency renderpassSubpassDependency = {
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = vkhpp::PipelineStageFlagBits::eColorAttachmentOutput,
      .srcAccessMask = {},
      .dstStageMask = vkhpp::PipelineStageFlagBits::eColorAttachmentOutput,
      .dstAccessMask = vkhpp::AccessFlagBits::eColorAttachmentWrite,
  };
  const vkhpp::RenderPassCreateInfo renderpassCreateInfo = {
      .attachmentCount = static_cast<uint32_t>(renderpassAttachments.size()),
      .pAttachments = renderpassAttachments.data(),
      .subpassCount = 1,
      .pSubpasses = &renderpassSubpass,
      .dependencyCount = 1,
      .pDependencies = &renderpassSubpassDependency,
  };
  mRenderpass =
      VK_EXPECT_RV(mDevice->createRenderPassUnique(renderpassCreateInfo));

  for (const auto imageView : swapchainInfo.swapchainImageViews) {
    const std::vector<vkhpp::ImageView> framebufferAttachments = {
        imageView,
    };
    const vkhpp::FramebufferCreateInfo framebufferCreateInfo = {
        .renderPass = *mRenderpass,
        .attachmentCount = static_cast<uint32_t>(framebufferAttachments.size()),
        .pAttachments = framebufferAttachments.data(),
        .width = swapchainInfo.swapchainExtent.width,
        .height = swapchainInfo.swapchainExtent.height,
        .layers = 1,
    };
    auto framebuffer =
        VK_EXPECT_RV(mDevice->createFramebufferUnique(framebufferCreateInfo));
    mSwapchainImageObjects.push_back(SwapchainImageObjects{
        .extent = swapchainInfo.swapchainExtent,
        .framebuffer = std::move(framebuffer),
    });
  }

  const std::vector<vkhpp::PipelineShaderStageCreateInfo> pipelineStages = {
      vkhpp::PipelineShaderStageCreateInfo{
          .stage = vkhpp::ShaderStageFlagBits::eVertex,
          .module = *mVertShaderModule,
          .pName = "main",
      },
      vkhpp::PipelineShaderStageCreateInfo{
          .stage = vkhpp::ShaderStageFlagBits::eFragment,
          .module = *mFragShaderModule,
          .pName = "main",
      },
  };

  const vkhpp::PipelineVertexInputStateCreateInfo
      pipelineVertexInputStateCreateInfo = {};
  const vkhpp::PipelineInputAssemblyStateCreateInfo
      pipelineInputAssemblyStateCreateInfo = {
          .topology = vkhpp::PrimitiveTopology::eTriangleStrip,
      };
  const vkhpp::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo =
      {
          .viewportCount = 1,
          .pViewports = nullptr,
          .scissorCount = 1,
          .pScissors = nullptr,
      };
  const vkhpp::PipelineRasterizationStateCreateInfo
      pipelineRasterStateCreateInfo = {
          .depthClampEnable = VK_FALSE,
          .rasterizerDiscardEnable = VK_FALSE,
          .polygonMode = vkhpp::PolygonMode::eFill,
          .cullMode = {},
          .frontFace = vkhpp::FrontFace::eCounterClockwise,
          .depthBiasEnable = VK_FALSE,
          .depthBiasConstantFactor = 0.0f,
          .depthBiasClamp = 0.0f,
          .depthBiasSlopeFactor = 0.0f,
          .lineWidth = 1.0f,
      };
  const vkhpp::SampleMask pipelineSampleMask = 65535;
  const vkhpp::PipelineMultisampleStateCreateInfo
      pipelineMultisampleStateCreateInfo = {
          .rasterizationSamples = vkhpp::SampleCountFlagBits::e1,
          .sampleShadingEnable = VK_FALSE,
          .minSampleShading = 1.0f,
          .pSampleMask = &pipelineSampleMask,
          .alphaToCoverageEnable = VK_FALSE,
          .alphaToOneEnable = VK_FALSE,
      };
  const vkhpp::PipelineDepthStencilStateCreateInfo
      pipelineDepthStencilStateCreateInfo = {
          .depthTestEnable = VK_FALSE,
          .depthWriteEnable = VK_FALSE,
          .depthCompareOp = vkhpp::CompareOp::eLess,
          .depthBoundsTestEnable = VK_FALSE,
          .stencilTestEnable = VK_FALSE,
          .front =
              {
                  .failOp = vkhpp::StencilOp::eKeep,
                  .passOp = vkhpp::StencilOp::eKeep,
                  .depthFailOp = vkhpp::StencilOp::eKeep,
                  .compareOp = vkhpp::CompareOp::eAlways,
                  .compareMask = 0,
                  .writeMask = 0,
                  .reference = 0,
              },
          .back =
              {
                  .failOp = vkhpp::StencilOp::eKeep,
                  .passOp = vkhpp::StencilOp::eKeep,
                  .depthFailOp = vkhpp::StencilOp::eKeep,
                  .compareOp = vkhpp::CompareOp::eAlways,
                  .compareMask = 0,
                  .writeMask = 0,
                  .reference = 0,
              },
          .minDepthBounds = 0.0f,
          .maxDepthBounds = 0.0f,
      };
  const std::vector<vkhpp::PipelineColorBlendAttachmentState>
      pipelineColorBlendAttachments = {
          vkhpp::PipelineColorBlendAttachmentState{
              .blendEnable = VK_FALSE,
              .srcColorBlendFactor = vkhpp::BlendFactor::eOne,
              .dstColorBlendFactor = vkhpp::BlendFactor::eOneMinusSrcAlpha,
              .colorBlendOp = vkhpp::BlendOp::eAdd,
              .srcAlphaBlendFactor = vkhpp::BlendFactor::eOne,
              .dstAlphaBlendFactor = vkhpp::BlendFactor::eOneMinusSrcAlpha,
              .alphaBlendOp = vkhpp::BlendOp::eAdd,
              .colorWriteMask = vkhpp::ColorComponentFlagBits::eR |
                                vkhpp::ColorComponentFlagBits::eG |
                                vkhpp::ColorComponentFlagBits::eB |
                                vkhpp::ColorComponentFlagBits::eA,
          },
      };
  const vkhpp::PipelineColorBlendStateCreateInfo
      pipelineColorBlendStateCreateInfo = {
          .logicOpEnable = VK_FALSE,
          .logicOp = vkhpp::LogicOp::eCopy,
          .attachmentCount =
              static_cast<uint32_t>(pipelineColorBlendAttachments.size()),
          .pAttachments = pipelineColorBlendAttachments.data(),
          .blendConstants = {{
              0.0f,
              0.0f,
              0.0f,
              0.0f,
          }},
      };
  const std::vector<vkhpp::DynamicState> pipelineDynamicStates = {
      vkhpp::DynamicState::eViewport,
      vkhpp::DynamicState::eScissor,
  };
  const vkhpp::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {
      .dynamicStateCount = static_cast<uint32_t>(pipelineDynamicStates.size()),
      .pDynamicStates = pipelineDynamicStates.data(),
  };
  const vkhpp::GraphicsPipelineCreateInfo pipelineCreateInfo = {
      .stageCount = static_cast<uint32_t>(pipelineStages.size()),
      .pStages = pipelineStages.data(),
      .pVertexInputState = &pipelineVertexInputStateCreateInfo,
      .pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo,
      .pTessellationState = nullptr,
      .pViewportState = &pipelineViewportStateCreateInfo,
      .pRasterizationState = &pipelineRasterStateCreateInfo,
      .pMultisampleState = &pipelineMultisampleStateCreateInfo,
      .pDepthStencilState = &pipelineDepthStencilStateCreateInfo,
      .pColorBlendState = &pipelineColorBlendStateCreateInfo,
      .pDynamicState = &pipelineDynamicStateCreateInfo,
      .layout = *mPipelineLayout,
      .renderPass = *mRenderpass,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = 0,
  };
  mPipeline = VK_EXPECT_RV(
      mDevice->createGraphicsPipelineUnique({}, pipelineCreateInfo));

  return Ok{};
}

Result<Ok> FullscreenColor::DestroySwapchainDependents() {
  mPipeline.reset();
  mSwapchainImageObjects.clear();
  mRenderpass.reset();
  return Ok{};
}

Result<Ok> FullscreenColor::RecordFrame(const FrameInfo& frame) {
  vkhpp::CommandBuffer commandBuffer = frame.commandBuffer;

  const SwapchainImageObjects& swapchainObjects =
      mSwapchainImageObjects[frame.swapchainImageIndex];

  const std::vector<vkhpp::ClearValue> renderPassBeginClearValues = {
      vkhpp::ClearValue{
          .color =
              {
                  .float32 = {{1.0f, 0.0f, 0.0f, 1.0f}},
              },
      },
  };
  const vkhpp::RenderPassBeginInfo renderPassBeginInfo = {
      .renderPass = *mRenderpass,
      .framebuffer = *swapchainObjects.framebuffer,
      .renderArea =
          {
              .offset =
                  {
                      .x = 0,
                      .y = 0,
                  },
              .extent = swapchainObjects.extent,
          },
      .clearValueCount =
          static_cast<uint32_t>(renderPassBeginClearValues.size()),
      .pClearValues = renderPassBeginClearValues.data(),
  };
  commandBuffer.beginRenderPass(renderPassBeginInfo,
                                vkhpp::SubpassContents::eInline);

  commandBuffer.bindPipeline(vkhpp::PipelineBindPoint::eGraphics, *mPipeline);

  const vkhpp::Viewport viewport = {
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<float>(swapchainObjects.extent.width),
      .height = static_cast<float>(swapchainObjects.extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  commandBuffer.setViewport(0, {viewport});

  const vkhpp::Rect2D scissor = {
      .offset =
          {
              .x = 0,
              .y = 0,
          },
      .extent = swapchainObjects.extent,
  };
  commandBuffer.setScissor(0, {scissor});

  commandBuffer.draw(4, 1, 0, 0);

  commandBuffer.endRenderPass();

  return Ok{};
}

}  // namespace cuttlefish
