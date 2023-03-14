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

#include "host/libs/graphics_detector/graphics_detector_vk_precision_qualifiers_on_yuv_samplers.h"

#include <vector>

#include <android-base/logging.h>

#include "host/libs/graphics_detector/img.h"
#include "host/libs/graphics_detector/subprocess.h"
#include "host/libs/graphics_detector/vk.h"

namespace cuttlefish {
namespace {

// kBlitTextureVert
#include "host/libs/graphics_detector/shaders/blit_texture.vert.inl"
// kBlitTextureFrag
#include "host/libs/graphics_detector/shaders/blit_texture.frag.inl"
// kBlitTextureLowpFrag
#include "host/libs/graphics_detector/shaders/blit_texture_lowp.frag.inl"
// kBlitTextureMediumpFrag
#include "host/libs/graphics_detector/shaders/blit_texture_mediump.frag.inl"
// kBlitTextureHighpFrag
#include "host/libs/graphics_detector/shaders/blit_texture_highp.frag.inl"

vk::Result CanHandlePrecisionQualifierWithYuvSampler(
    const std::vector<uint8_t>& blit_vert_shader_spirv,
    const std::vector<uint8_t>& blit_frag_shader_spirv, bool* out_passed_test) {
  std::optional<Vk> vk = Vk::Load(
      /*instance_extensions=*/{},
      /*instance_layers=*/{},
      /*device_extensions=*/
      {
          VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
      });
  if (!vk) {
    LOG(FATAL) << "Failed to load vk";
  }

  uint32_t texture_width = 32;
  uint32_t texture_height = 32;
  std::vector<uint8_t> texture_data_rgba8888;
  FillWithColor(texture_width, texture_height,
                /*red=*/0xFF,
                /*green=*/0x00,
                /*blue=*/0x00,
                /*alpha=*/0xFF, &texture_data_rgba8888);

  std::vector<uint8_t> texture_data_yuv420_y;
  std::vector<uint8_t> texture_data_yuv420_u;
  std::vector<uint8_t> texture_data_yuv420_v;
  ConvertRGBA8888ToYUV420(texture_width, texture_height, texture_data_rgba8888,
                          &texture_data_yuv420_y, &texture_data_yuv420_u,
                          &texture_data_yuv420_v);

#if 0
    // Debugging can be easier with a larger image with more details.
    texture_data_yuv420_y.clear();
    texture_data_yuv420_u.clear();
    texture_data_yuv420_v.clear();
    LoadYUV420FromBitmapFile("custom.bmp",
                             &texture_width,
                             &texture_height,
                             &texture_data_yuv420_y,
                             &texture_data_yuv420_u,
                             &texture_data_yuv420_v);
#endif

  Vk::YuvImageWithMemory sampled_image = VK_EXPECT_RESULT(vk->CreateYuvImage(
      texture_width, texture_height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
          vk::ImageUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      vk::ImageLayout::eTransferDstOptimal));

  VK_ASSERT(vk->LoadYuvImage(
      sampled_image.image, texture_width, texture_height, texture_data_yuv420_y,
      texture_data_yuv420_u, texture_data_yuv420_v,
      /*current_layout=*/vk::ImageLayout::eTransferDstOptimal,
      /*returned_layout=*/vk::ImageLayout::eShaderReadOnlyOptimal));

  Vk::FramebufferWithAttachments framebuffer =
      VK_EXPECT_RESULT(vk->CreateFramebuffer(
          texture_width, texture_height,
          /*color_attachment_format=*/vk::Format::eR8G8B8A8Unorm));

  const vk::Sampler descriptor_set_0_binding_0_sampler =
      *sampled_image.image_sampler;
  const std::vector<vk::DescriptorSetLayoutBinding> descriptor_set_0_bindings =
      {
          vk::DescriptorSetLayoutBinding{
              .binding = 0,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler,
              .descriptorCount = 1,
              .stageFlags = vk::ShaderStageFlagBits::eFragment,
              .pImmutableSamplers = &descriptor_set_0_binding_0_sampler,
          },
      };
  const vk::DescriptorSetLayoutCreateInfo descriptor_set_0_create_info = {
      .bindingCount = static_cast<uint32_t>(descriptor_set_0_bindings.size()),
      .pBindings = descriptor_set_0_bindings.data(),
  };
  auto descriptor_set_0_layout =
      VK_EXPECT_RESULT(vk::raii::DescriptorSetLayout::create(
          vk->vk_device, descriptor_set_0_create_info));

  const std::vector<vk::DescriptorPoolSize> descriptor_pool_sizes = {
      vk::DescriptorPoolSize{
          .type = vk::DescriptorType::eCombinedImageSampler,
          .descriptorCount = 1,
      },
  };
  const vk::DescriptorPoolCreateInfo descriptor_pool_create_info = {
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(descriptor_pool_sizes.size()),
      .pPoolSizes = descriptor_pool_sizes.data(),
  };
  auto descriptor_set_0_pool =
      VK_EXPECT_RESULT(vk::raii::DescriptorPool::create(
          vk->vk_device, descriptor_pool_create_info));

  const vk::DescriptorSetLayout descriptor_set_0_layout_handle =
      *descriptor_set_0_layout;
  const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info = {
      .descriptorPool = *descriptor_set_0_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptor_set_0_layout_handle,
  };
  auto descriptor_sets = VK_EXPECT_RESULT(vk::raii::DescriptorSets::create(
      vk->vk_device, descriptor_set_allocate_info));
  auto descriptor_set_0(std::move(descriptor_sets[0]));

  const vk::DescriptorImageInfo descriptor_set_0_binding_0_image_info = {
      .sampler = VK_NULL_HANDLE,
      .imageView = *sampled_image.image_view,
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
  };
  const std::vector<vk::WriteDescriptorSet> descriptor_set_0_writes = {
      vk::WriteDescriptorSet{
          .dstSet = *descriptor_set_0,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eCombinedImageSampler,
          .pImageInfo = &descriptor_set_0_binding_0_image_info,
          .pBufferInfo = nullptr,
          .pTexelBufferView = nullptr,
      },
  };
  vk->vk_device.updateDescriptorSets(descriptor_set_0_writes, {});

  const std::vector<vk::DescriptorSetLayout>
      pipeline_layout_descriptor_set_layouts = {
          *descriptor_set_0_layout,
      };
  const vk::PipelineLayoutCreateInfo pipeline_layout_create_info = {
      .setLayoutCount =
          static_cast<uint32_t>(pipeline_layout_descriptor_set_layouts.size()),
      .pSetLayouts = pipeline_layout_descriptor_set_layouts.data(),
  };
  auto pipeline_layout = VK_EXPECT_RESULT(vk::raii::PipelineLayout::create(
      vk->vk_device, pipeline_layout_create_info));

  const vk::ShaderModuleCreateInfo vert_shader_create_info = {
      .codeSize = static_cast<uint32_t>(blit_vert_shader_spirv.size()),
      .pCode = reinterpret_cast<const uint32_t*>(blit_vert_shader_spirv.data()),
  };
  auto vert_shader_module = VK_EXPECT_RESULT(
      vk::raii::ShaderModule::create(vk->vk_device, vert_shader_create_info));

  const vk::ShaderModuleCreateInfo frag_shader_create_info = {
      .codeSize = static_cast<uint32_t>(blit_frag_shader_spirv.size()),
      .pCode = reinterpret_cast<const uint32_t*>(blit_frag_shader_spirv.data()),
  };
  auto frag_shader_module = VK_EXPECT_RESULT(
      vk::raii::ShaderModule::create(vk->vk_device, frag_shader_create_info));

  const std::vector<vk::PipelineShaderStageCreateInfo> pipeline_stages = {
      vk::PipelineShaderStageCreateInfo{
          .stage = vk::ShaderStageFlagBits::eVertex,
          .module = *vert_shader_module,
          .pName = "main",
      },
      vk::PipelineShaderStageCreateInfo{
          .stage = vk::ShaderStageFlagBits::eFragment,
          .module = *frag_shader_module,
          .pName = "main",
      },
  };
  const vk::PipelineVertexInputStateCreateInfo
      pipeline_vertex_input_state_create_info = {};
  const vk::PipelineInputAssemblyStateCreateInfo
      pipeline_input_assembly_state_create_info = {
          .topology = vk::PrimitiveTopology::eTriangleStrip,
      };
  const vk::PipelineViewportStateCreateInfo
      pipeline_viewport_state_create_info = {
          .viewportCount = 1,
          .pViewports = nullptr,
          .scissorCount = 1,
          .pScissors = nullptr,
      };
  const vk::PipelineRasterizationStateCreateInfo
      pipeline_rasterization_state_create_info = {
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
  const vk::SampleMask pipeline_sample_mask = 65535;
  const vk::PipelineMultisampleStateCreateInfo
      pipeline_multisample_state_create_info = {
          .rasterizationSamples = vk::SampleCountFlagBits::e1,
          .sampleShadingEnable = VK_FALSE,
          .minSampleShading = 1.0f,
          .pSampleMask = &pipeline_sample_mask,
          .alphaToCoverageEnable = VK_FALSE,
          .alphaToOneEnable = VK_FALSE,
      };
  const vk::PipelineDepthStencilStateCreateInfo
      pipeline_depth_stencil_state_create_info = {
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
      pipeline_color_blend_attachments = {
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
      pipeline_color_blend_state_create_info = {
          .logicOpEnable = VK_FALSE,
          .logicOp = vk::LogicOp::eCopy,
          .attachmentCount =
              static_cast<uint32_t>(pipeline_color_blend_attachments.size()),
          .pAttachments = pipeline_color_blend_attachments.data(),
          .blendConstants = {{
              0.0f,
              0.0f,
              0.0f,
              0.0f,
          }},
      };
  const std::vector<vk::DynamicState> pipeline_dynamic_states = {
      vk::DynamicState::eViewport,
      vk::DynamicState::eScissor,
  };
  const vk::PipelineDynamicStateCreateInfo pipeline_dynamic_state_create_info =
      {
          .dynamicStateCount =
              static_cast<uint32_t>(pipeline_dynamic_states.size()),
          .pDynamicStates = pipeline_dynamic_states.data(),
      };
  const vk::GraphicsPipelineCreateInfo pipeline_create_info = {
      .stageCount = static_cast<uint32_t>(pipeline_stages.size()),
      .pStages = pipeline_stages.data(),
      .pVertexInputState = &pipeline_vertex_input_state_create_info,
      .pInputAssemblyState = &pipeline_input_assembly_state_create_info,
      .pTessellationState = nullptr,
      .pViewportState = &pipeline_viewport_state_create_info,
      .pRasterizationState = &pipeline_rasterization_state_create_info,
      .pMultisampleState = &pipeline_multisample_state_create_info,
      .pDepthStencilState = &pipeline_depth_stencil_state_create_info,
      .pColorBlendState = &pipeline_color_blend_state_create_info,
      .pDynamicState = &pipeline_dynamic_state_create_info,
      .layout = *pipeline_layout,
      .renderPass = *framebuffer.renderpass,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = 0,
  };
  auto pipeline = VK_EXPECT_RESULT(
      vk::raii::Pipeline::create(vk->vk_device, nullptr, pipeline_create_info));

  VK_RETURN_IF_NOT_SUCCESS(
      vk->DoCommandsImmediate([&](vk::raii::CommandBuffer& command_buffer) {
        const std::vector<vk::ClearValue> render_pass_begin_clear_values = {
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
        const vk::RenderPassBeginInfo render_pass_begin_info = {
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
                            .width = texture_width,
                            .height = texture_height,
                        },
                },
            .clearValueCount =
                static_cast<uint32_t>(render_pass_begin_clear_values.size()),
            .pClearValues = render_pass_begin_clear_values.data(),
        };
        command_buffer.beginRenderPass(render_pass_begin_info,
                                       vk::SubpassContents::eInline);

        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                    *pipeline);

        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          *pipeline_layout,
                                          /*firstSet=*/0, {*descriptor_set_0},
                                          /*dynamicOffsets=*/{});
        const vk::Viewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(texture_width),
            .height = static_cast<float>(texture_height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        command_buffer.setViewport(0, {viewport});

        const vk::Rect2D scissor = {
            .offset =
                {
                    .x = 0,
                    .y = 0,
                },
            .extent =
                {
                    .width = texture_width,
                    .height = texture_height,
                },
        };
        command_buffer.setScissor(0, {scissor});

        command_buffer.draw(4, 1, 0, 0);

        command_buffer.endRenderPass();
        return vk::Result::eSuccess;
      }));

  std::vector<uint8_t> rendered_pixels;
  VK_RETURN_IF_NOT_SUCCESS(vk->DownloadImage(
      texture_width, texture_height, framebuffer.color_attachment->image,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::eColorAttachmentOptimal, &rendered_pixels));
#if 0
    SaveRGBAToBitmapFile(texture_width,
                         texture_height,
                         rendered_pixels.data(),
                         "rendered.bmp");
#endif

  *out_passed_test = ImagesAreSimilar(texture_width, texture_height,
                                      texture_data_rgba8888, rendered_pixels);
  return vk::Result::eSuccess;
}

void PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirkImpl(
    GraphicsAvailability* availability) {
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
  for (const auto& combo : combos) {
    bool passed_test = false;
    auto result = CanHandlePrecisionQualifierWithYuvSampler(
        combo.vert, combo.frag, &passed_test);
    if (result != vk::Result::eSuccess) {
      LOG(ERROR) << "Failed to fully check if driver has issue when "
                 << combo.name;
      availability->vulkan_has_issue_with_precision_qualifiers_on_yuv_samplers =
          true;
      return;
    }
    if (!passed_test) {
      LOG(ERROR) << "Driver has issue when " << combo.name;
      availability->vulkan_has_issue_with_precision_qualifiers_on_yuv_samplers =
          true;
      return;
    }
  }
}

}  // namespace

void PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirk(
    GraphicsAvailability* availability) {
  auto result = DoWithSubprocessCheck(
      "PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirk", [&]() {
        PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirkImpl(availability);
      });
  if (result == SubprocessResult::kFailure) {
    availability->vulkan_has_issue_with_precision_qualifiers_on_yuv_samplers =
        true;
  }
}

}  // namespace cuttlefish