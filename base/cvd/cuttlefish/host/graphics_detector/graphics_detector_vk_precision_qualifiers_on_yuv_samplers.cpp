/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/graphics_detector/graphics_detector_vk_precision_qualifiers_on_yuv_samplers.h"

#include <vector>

#include "cuttlefish/host/graphics_detector/image.h"
#include "cuttlefish/host/graphics_detector/vulkan.h"

namespace gfxstream {
namespace {

// kBlitTextureVert
#include "cuttlefish/host/graphics_detector/shaders/blit_texture.vert.inl"
// kBlitTextureFrag
#include "cuttlefish/host/graphics_detector/shaders/blit_texture.frag.inl"
// kBlitTextureLowpFrag
#include "cuttlefish/host/graphics_detector/shaders/blit_texture_lowp.frag.inl"
// kBlitTextureMediumpFrag
#include "cuttlefish/host/graphics_detector/shaders/blit_texture_mediump.frag.inl"
// kBlitTextureHighpFrag
#include "cuttlefish/host/graphics_detector/shaders/blit_texture_highp.frag.inl"

gfxstream::expected<bool, vk::Result> CanHandlePrecisionQualifierWithYuvSampler(
    const std::vector<uint8_t>& blitVertShaderSpirv,
    const std::vector<uint8_t>& blitFragShaderSpirv) {
  auto vk = VK_EXPECT(Vk::Load(
      /*instance_extensions=*/{},
      /*instance_layers=*/{},
      /*device_extensions=*/
      {
          VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
      }));

  uint32_t textureWidth = 32;
  uint32_t textureHeight = 32;
  RGBAImage textureDataRgba = FillWithColor(textureWidth, textureHeight,
                                            /*red=*/0xFF,
                                            /*green=*/0x00,
                                            /*blue=*/0x00,
                                            /*alpha=*/0xFF);

  YUV420Image textureDataYuv = ConvertRGBA8888ToYUV420(textureDataRgba);
#if 0
        // Debugging can be easier with a larger image with more details.
        textureDataYuv = GFXSTREAM_EXPECT(LoadYUV420FromBitmapFile("custom.bmp"));
#endif

  Vk::YuvImageWithMemory sampledImage = VK_EXPECT(vk.CreateYuvImage(
      textureWidth, textureHeight,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
          vk::ImageUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      vk::ImageLayout::eTransferDstOptimal));

  VK_EXPECT_RESULT(vk.LoadYuvImage(
      sampledImage.image, textureWidth, textureHeight, textureDataYuv.y,
      textureDataYuv.u, textureDataYuv.v,
      /*currentLayout=*/vk::ImageLayout::eTransferDstOptimal,
      /*returnedLayout=*/vk::ImageLayout::eShaderReadOnlyOptimal));

  Vk::FramebufferWithAttachments framebuffer = VK_EXPECT(vk.CreateFramebuffer(
      textureWidth, textureHeight,
      /*colorAttachmentFormat=*/vk::Format::eR8G8B8A8Unorm));

  const vk::Sampler descriptorSet0Binding0Sampler = *sampledImage.imageSampler;
  const std::vector<vk::DescriptorSetLayoutBinding> descriptorSet0Bindings = {
      vk::DescriptorSetLayoutBinding{
          .binding = 0,
          .descriptorType = vk::DescriptorType::eCombinedImageSampler,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eFragment,
          .pImmutableSamplers = &descriptorSet0Binding0Sampler,
      },
  };
  const vk::DescriptorSetLayoutCreateInfo descriptorSet0CreateInfo = {
      .bindingCount = static_cast<uint32_t>(descriptorSet0Bindings.size()),
      .pBindings = descriptorSet0Bindings.data(),
  };
  auto descriptorSet0Layout = VK_EXPECT_RV(
      vk.device().createDescriptorSetLayoutUnique(descriptorSet0CreateInfo));

  const std::vector<vk::DescriptorPoolSize> descriptorPoolSizes = {
      vk::DescriptorPoolSize{
          .type = vk::DescriptorType::eCombinedImageSampler,
          .descriptorCount = 1,
      },
  };
  const vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo = {
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
      .pPoolSizes = descriptorPoolSizes.data(),
  };
  auto descriptorSet0Pool = VK_EXPECT_RV(
      vk.device().createDescriptorPoolUnique(descriptorPoolCreateInfo));

  const vk::DescriptorSetLayout descriptorSet0LayoutHandle =
      *descriptorSet0Layout;
  const vk::DescriptorSetAllocateInfo descriptorSet0AllocateInfo = {
      .descriptorPool = *descriptorSet0Pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptorSet0LayoutHandle,
  };
  auto descriptorSets = VK_EXPECT_RV(
      vk.device().allocateDescriptorSetsUnique(descriptorSet0AllocateInfo));
  auto descriptorSet0(std::move(descriptorSets[0]));

  const vk::DescriptorImageInfo descriptorSet0Binding0ImageInfo = {
      .sampler = VK_NULL_HANDLE,
      .imageView = *sampledImage.imageView,
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
  };
  const std::vector<vk::WriteDescriptorSet> descriptorSet0Writes = {
      vk::WriteDescriptorSet{
          .dstSet = *descriptorSet0,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eCombinedImageSampler,
          .pImageInfo = &descriptorSet0Binding0ImageInfo,
          .pBufferInfo = nullptr,
          .pTexelBufferView = nullptr,
      },
  };
  vk.device().updateDescriptorSets(descriptorSet0Writes, {});

  const std::vector<vk::DescriptorSetLayout>
      pipelineLayoutDescriptorSetLayouts = {
          *descriptorSet0Layout,
      };
  const vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
      .setLayoutCount =
          static_cast<uint32_t>(pipelineLayoutDescriptorSetLayouts.size()),
      .pSetLayouts = pipelineLayoutDescriptorSetLayouts.data(),
  };
  auto pipelineLayout = VK_EXPECT_RV(
      vk.device().createPipelineLayoutUnique(pipelineLayoutCreateInfo));

  const vk::ShaderModuleCreateInfo vertShaderCreateInfo = {
      .codeSize = static_cast<uint32_t>(blitVertShaderSpirv.size()),
      .pCode = reinterpret_cast<const uint32_t*>(blitVertShaderSpirv.data()),
  };
  auto vertShaderModule =
      VK_EXPECT_RV(vk.device().createShaderModuleUnique(vertShaderCreateInfo));

  const vk::ShaderModuleCreateInfo fragShaderCreateInfo = {
      .codeSize = static_cast<uint32_t>(blitFragShaderSpirv.size()),
      .pCode = reinterpret_cast<const uint32_t*>(blitFragShaderSpirv.data()),
  };
  auto fragShaderModule =
      VK_EXPECT_RV(vk.device().createShaderModuleUnique(fragShaderCreateInfo));

  const std::vector<vk::PipelineShaderStageCreateInfo> pipelineStages = {
      vk::PipelineShaderStageCreateInfo{
          .stage = vk::ShaderStageFlagBits::eVertex,
          .module = *vertShaderModule,
          .pName = "main",
      },
      vk::PipelineShaderStageCreateInfo{
          .stage = vk::ShaderStageFlagBits::eFragment,
          .module = *fragShaderModule,
          .pName = "main",
      },
  };
  const vk::PipelineVertexInputStateCreateInfo
      pipelineVertexInputStateCreateInfo = {};
  const vk::PipelineInputAssemblyStateCreateInfo
      pipelineInputAssemblyStateCreateInfo = {
          .topology = vk::PrimitiveTopology::eTriangleStrip,
      };
  const vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo = {
      .viewportCount = 1,
      .pViewports = nullptr,
      .scissorCount = 1,
      .pScissors = nullptr,
  };
  const vk::PipelineRasterizationStateCreateInfo pipelineRasterStateCreateInfo =
      {
          .depthClampEnable = VK_FALSE,
          .rasterizerDiscardEnable = VK_FALSE,
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = {},
          .frontFace = vk::FrontFace::eCounterClockwise,
          .depthBiasEnable = VK_FALSE,
          .depthBiasConstantFactor = 0.0f,
          .depthBiasClamp = 0.0f,
          .depthBiasSlopeFactor = 0.0f,
          .lineWidth = 1.0f,
      };
  const vk::SampleMask pipelineSampleMask = 65535;
  const vk::PipelineMultisampleStateCreateInfo
      pipelineMultisampleStateCreateInfo = {
          .rasterizationSamples = vk::SampleCountFlagBits::e1,
          .sampleShadingEnable = VK_FALSE,
          .minSampleShading = 1.0f,
          .pSampleMask = &pipelineSampleMask,
          .alphaToCoverageEnable = VK_FALSE,
          .alphaToOneEnable = VK_FALSE,
      };
  const vk::PipelineDepthStencilStateCreateInfo
      pipelineDepthStencilStateCreateInfo = {
          .depthTestEnable = VK_FALSE,
          .depthWriteEnable = VK_FALSE,
          .depthCompareOp = vk::CompareOp::eLess,
          .depthBoundsTestEnable = VK_FALSE,
          .stencilTestEnable = VK_FALSE,
          .front =
              {
                  .failOp = vk::StencilOp::eKeep,
                  .passOp = vk::StencilOp::eKeep,
                  .depthFailOp = vk::StencilOp::eKeep,
                  .compareOp = vk::CompareOp::eAlways,
                  .compareMask = 0,
                  .writeMask = 0,
                  .reference = 0,
              },
          .back =
              {
                  .failOp = vk::StencilOp::eKeep,
                  .passOp = vk::StencilOp::eKeep,
                  .depthFailOp = vk::StencilOp::eKeep,
                  .compareOp = vk::CompareOp::eAlways,
                  .compareMask = 0,
                  .writeMask = 0,
                  .reference = 0,
              },
          .minDepthBounds = 0.0f,
          .maxDepthBounds = 0.0f,
      };
  const std::vector<vk::PipelineColorBlendAttachmentState>
      pipelineColorBlendAttachments = {
          vk::PipelineColorBlendAttachmentState{
              .blendEnable = VK_FALSE,
              .srcColorBlendFactor = vk::BlendFactor::eOne,
              .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
              .colorBlendOp = vk::BlendOp::eAdd,
              .srcAlphaBlendFactor = vk::BlendFactor::eOne,
              .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
              .alphaBlendOp = vk::BlendOp::eAdd,
              .colorWriteMask = vk::ColorComponentFlagBits::eR |
                                vk::ColorComponentFlagBits::eG |
                                vk::ColorComponentFlagBits::eB |
                                vk::ColorComponentFlagBits::eA,
          },
      };
  const vk::PipelineColorBlendStateCreateInfo
      pipelineColorBlendStateCreateInfo = {
          .logicOpEnable = VK_FALSE,
          .logicOp = vk::LogicOp::eCopy,
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
  const std::vector<vk::DynamicState> pipelineDynamicStates = {
      vk::DynamicState::eViewport,
      vk::DynamicState::eScissor,
  };
  const vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {
      .dynamicStateCount = static_cast<uint32_t>(pipelineDynamicStates.size()),
      .pDynamicStates = pipelineDynamicStates.data(),
  };
  const vk::GraphicsPipelineCreateInfo pipelineCreateInfo = {
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
      .layout = *pipelineLayout,
      .renderPass = *framebuffer.renderpass,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = 0,
  };
  auto pipeline = VK_EXPECT_RV(
      vk.device().createGraphicsPipelineUnique({}, pipelineCreateInfo));

  VK_EXPECT_RESULT(vk.DoCommandsImmediate([&](vk::UniqueCommandBuffer& cmd) {
    const std::vector<vk::ClearValue> renderPassBeginClearValues = {
        vk::ClearValue{
            .color =
                {
                    .float32 = {{
                        1.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    }},
                },
        },
    };
    const vk::RenderPassBeginInfo renderPassBeginInfo = {
        .renderPass = *framebuffer.renderpass,
        .framebuffer = *framebuffer.framebuffer,
        .renderArea =
            {
                .offset =
                    {
                        .x = 0,
                        .y = 0,
                    },
                .extent =
                    {
                        .width = textureWidth,
                        .height = textureHeight,
                    },
            },
        .clearValueCount =
            static_cast<uint32_t>(renderPassBeginClearValues.size()),
        .pClearValues = renderPassBeginClearValues.data(),
    };
    cmd->beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

    cmd->bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

    cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout,
                            /*firstSet=*/0, {*descriptorSet0},
                            /*dynamicOffsets=*/{});

    const vk::Viewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(textureWidth),
        .height = static_cast<float>(textureHeight),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    cmd->setViewport(0, {viewport});

    const vk::Rect2D scissor = {
        .offset =
            {
                .x = 0,
                .y = 0,
            },
        .extent =
            {
                .width = textureWidth,
                .height = textureHeight,
            },
    };
    cmd->setScissor(0, {scissor});

    cmd->draw(4, 1, 0, 0);

    cmd->endRenderPass();
    return vk::Result::eSuccess;
  }));

  const std::vector<uint8_t> renderedPixels = VK_EXPECT(vk.DownloadImage(
      textureWidth, textureHeight, framebuffer.colorAttachment->image,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::eColorAttachmentOptimal));

#if 0
        SaveRGBAToBitmapFile(textureWidth,
                             textureHeight,
                             renderedPixels.data(),
                             "rendered.bmp");
#endif

  const RGBAImage actual = {
      .width = textureWidth,
      .height = textureHeight,
      .pixels = std::move(renderedPixels),
  };

  auto result = CompareImages(textureDataRgba, actual);
  return result.ok();
}

}  // namespace

gfxstream::expected<Ok, std::string>
PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirk(
    ::gfxstream::proto::GraphicsAvailability* availability) {
  struct ShaderCombo {
    std::string name;
    const std::vector<uint8_t>& vert;
    const std::vector<uint8_t>& frag;
  };
  const std::vector<ShaderCombo> combos = {
      ShaderCombo{
          .name = "sampler2D has no precision qualifier",
          .vert = kBlitTextureVert,
          .frag = kBlitTextureFrag,
      },
      ShaderCombo{
          .name = "sampler2D has a 'lowp' precision qualifier",
          .vert = kBlitTextureVert,
          .frag = kBlitTextureLowpFrag,
      },
      ShaderCombo{
          .name = "sampler2D has a 'mediump' precision qualifier",
          .vert = kBlitTextureVert,
          .frag = kBlitTextureMediumpFrag,
      },
      ShaderCombo{
          .name = "sampler2D has a 'highp' precision qualifier",
          .vert = kBlitTextureVert,
          .frag = kBlitTextureHighpFrag,
      },
  };

  bool anyTestFailed = false;
  for (const auto& combo : combos) {
    auto result =
        CanHandlePrecisionQualifierWithYuvSampler(combo.vert, combo.frag);
    if (!result.ok()) {
      // Failed to run to completion.
      return gfxstream::unexpected(vk::to_string(result.error()));
    }
    const bool passedTest = result.value();
    if (!passedTest) {
      // Ran to completion but had bad value.
      anyTestFailed = true;
      break;
    }
  }

  // TODO: Run this test per device.
  availability->mutable_vulkan()
      ->mutable_physical_devices(0)
      ->mutable_quirks()
      ->set_has_issue_with_precision_qualifiers_on_yuv_samplers(anyTestFailed);
  return Ok{};
}

}  // namespace gfxstream