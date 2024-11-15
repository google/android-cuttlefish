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

#include "fullscreen_texture.h"

#include "image.h"

namespace cuttlefish {
namespace {

#include "fullscreen_texture.frag.inl"
#include "fullscreen_texture.vert.inl"

}  // namespace

Result<std::unique_ptr<SampleBase>> BuildVulkanSampleApp() {
  return FullscreenTexture::Create();
}

/*static*/
Result<std::unique_ptr<SampleBase>> FullscreenTexture::Create() {
  std::unique_ptr<SampleBase> sample(new FullscreenTexture());
  VK_EXPECT(sample->StartUp());
  return sample;
}

Result<Ok> FullscreenTexture::StartUp() {
  VK_EXPECT(StartUpBase());

  const uint32_t imageWidth = 32;
  const uint32_t imageHeight = 32;

  mTexture = VK_EXPECT(CreateImage(imageWidth, imageHeight,
                                   vkhpp::Format::eR8G8B8A8Unorm,
                                   vkhpp::ImageUsageFlagBits::eSampled |
                                       vkhpp::ImageUsageFlagBits::eTransferDst,
                                   vkhpp::MemoryPropertyFlagBits::eDeviceLocal,
                                   vkhpp::ImageLayout::eTransferDstOptimal));

  const std::vector<uint8_t> imageContents = CreateImageContentsWithFourCorners(
      imageWidth, imageHeight,
      // clang-format off
        /*bottomLeft=*/  RGBA8888{.r = 255, .g =    0, .b =   0, .a = 255},
        /*bottomRight=*/ RGBA8888{.r =   0, .g =  255, .b =   0, .a = 255},
        /*topLeft=*/     RGBA8888{.r =   0, .g =    0, .b = 255, .a = 255},
        /*topRight=*/    RGBA8888{.r = 255, .g =  255, .b = 255, .a = 255}  // clang-format on
  );

  VK_EXPECT(
      LoadImage(mTexture.image, imageWidth, imageHeight, imageContents,
                /*currentLayout=*/vkhpp::ImageLayout::eTransferDstOptimal,
                /*returnedLayout=*/vkhpp::ImageLayout::eShaderReadOnlyOptimal));

  const vkhpp::SamplerCreateInfo samplerCreateInfo = {
      .magFilter = vkhpp::Filter::eNearest,
      .minFilter = vkhpp::Filter::eNearest,
      .mipmapMode = vkhpp::SamplerMipmapMode::eNearest,
      .addressModeU = vkhpp::SamplerAddressMode::eClampToEdge,
      .addressModeV = vkhpp::SamplerAddressMode::eClampToEdge,
      .addressModeW = vkhpp::SamplerAddressMode::eClampToEdge,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .compareOp = vkhpp::CompareOp::eLessOrEqual,
      .minLod = 0.0f,
      .maxLod = 0.25f,
      .borderColor = vkhpp::BorderColor::eIntTransparentBlack,
      .unnormalizedCoordinates = VK_FALSE,
  };
  mTextureSampler =
      VK_EXPECT_RV(mDevice->createSamplerUnique(samplerCreateInfo));

  const vkhpp::ShaderModuleCreateInfo vertShaderCreateInfo = {
      .codeSize = static_cast<uint32_t>(kFullscreenTextureVert.size()),
      .pCode = reinterpret_cast<const uint32_t*>(kFullscreenTextureVert.data()),
  };
  mVertShaderModule =
      VK_EXPECT_RV(mDevice->createShaderModuleUnique(vertShaderCreateInfo));

  const vkhpp::ShaderModuleCreateInfo fragShaderCreateInfo = {
      .codeSize = static_cast<uint32_t>(kFullscreenTextureFrag.size()),
      .pCode = reinterpret_cast<const uint32_t*>(kFullscreenTextureFrag.data()),
  };
  mFragShaderModule =
      VK_EXPECT_RV(mDevice->createShaderModuleUnique(fragShaderCreateInfo));

  const vkhpp::Sampler descriptorSet0Binding0Sampler = *mTextureSampler;
  const std::vector<vkhpp::DescriptorSetLayoutBinding> descriptorSet0Bindings =
      {
          vkhpp::DescriptorSetLayoutBinding{
              .binding = 0,
              .descriptorType = vkhpp::DescriptorType::eCombinedImageSampler,
              .descriptorCount = 1,
              .stageFlags = vkhpp::ShaderStageFlagBits::eFragment,
              .pImmutableSamplers = &descriptorSet0Binding0Sampler,
          },
      };
  const vkhpp::DescriptorSetLayoutCreateInfo descriptorSet0CreateInfo = {
      .bindingCount = static_cast<uint32_t>(descriptorSet0Bindings.size()),
      .pBindings = descriptorSet0Bindings.data(),
  };
  mDescriptorSet0Layout = VK_EXPECT_RV(
      mDevice->createDescriptorSetLayoutUnique(descriptorSet0CreateInfo));

  const std::vector<vkhpp::DescriptorPoolSize> descriptorPoolSizes = {
      vkhpp::DescriptorPoolSize{
          .type = vkhpp::DescriptorType::eCombinedImageSampler,
          .descriptorCount = 1,
      },
  };
  const vkhpp::DescriptorPoolCreateInfo descriptorPoolCreateInfo = {
      .flags = vkhpp::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
      .pPoolSizes = descriptorPoolSizes.data(),
  };
  mDescriptorSet0Pool = VK_EXPECT_RV(
      mDevice->createDescriptorPoolUnique(descriptorPoolCreateInfo));

  const vkhpp::DescriptorSetLayout descriptorSet0LayoutHandle =
      *mDescriptorSet0Layout;
  const vkhpp::DescriptorSetAllocateInfo descriptorSet0AllocateInfo = {
      .descriptorPool = *mDescriptorSet0Pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptorSet0LayoutHandle,
  };
  auto descriptorSets = VK_EXPECT_RV(
      mDevice->allocateDescriptorSetsUnique(descriptorSet0AllocateInfo));
  mDescriptorSet0 = std::move(descriptorSets[0]);

  const vkhpp::DescriptorImageInfo descriptorSet0Binding0ImageInfo = {
      .sampler = VK_NULL_HANDLE,
      .imageView = *mTexture.imageView,
      .imageLayout = vkhpp::ImageLayout::eShaderReadOnlyOptimal,
  };
  const std::vector<vkhpp::WriteDescriptorSet> descriptorSet0Writes = {
      vkhpp::WriteDescriptorSet{
          .dstSet = *mDescriptorSet0,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vkhpp::DescriptorType::eCombinedImageSampler,
          .pImageInfo = &descriptorSet0Binding0ImageInfo,
          .pBufferInfo = nullptr,
          .pTexelBufferView = nullptr,
      },
  };
  mDevice->updateDescriptorSets(descriptorSet0Writes, {});

  const std::vector<vkhpp::DescriptorSetLayout>
      pipelineLayoutDescriptorSetLayouts = {
          *mDescriptorSet0Layout,
      };
  const vkhpp::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
      .setLayoutCount =
          static_cast<uint32_t>(pipelineLayoutDescriptorSetLayouts.size()),
      .pSetLayouts = pipelineLayoutDescriptorSetLayouts.data(),
  };
  mPipelineLayout = VK_EXPECT_RV(
      mDevice->createPipelineLayoutUnique(pipelineLayoutCreateInfo));

  return Ok{};
}

Result<Ok> FullscreenTexture::CleanUp() {
  VK_EXPECT(CleanUpBase());

  mDevice->waitIdle();

  return Ok{};
}

Result<Ok> FullscreenTexture::CreateSwapchainDependents(
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

Result<Ok> FullscreenTexture::DestroySwapchainDependents() {
  mPipeline.reset();
  mSwapchainImageObjects.clear();
  mRenderpass.reset();
  return Ok{};
}

Result<Ok> FullscreenTexture::RecordFrame(const FrameInfo& frame) {
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

  commandBuffer.bindDescriptorSets(vkhpp::PipelineBindPoint::eGraphics,
                                   *mPipelineLayout,
                                   /*firstSet=*/0, {*mDescriptorSet0},
                                   /*dynamicOffsets=*/{});

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
