#pragma once

#include "vk_common.hpp"

#include "sys_os_api.h"

#include "vk_utils.hpp"

#include <vector>

static constexpr bool colorBlending = 0;

// TODO: variable entry point
constexpr char SHADER_ENTRY_POINT[] = "main";

// TODO: store more stuff ?
struct vk_gfx_pipeline_state
{
	VkPolygonMode		polyMode;
	VkCullModeFlags		cullFlags;
	VkFrontFace			frontFace;
	VkPrimitiveTopology primTopology;
	VkBlendFactor       srcColorBlendFactor;
	VkBlendFactor       dstColorBlendFactor;
	VkBlendFactor       srcAlphaBlendFactor;
	VkBlendFactor       dstAlphaBlendFactor;
	float               extraPrimitiveOverestimationSize;
	bool                conservativeRasterEnable;
	bool				depthWrite;
	bool				depthTestEnable;
	bool				blendCol;
};

static constexpr vk_gfx_pipeline_state DEFAULT_GFX_STATE = {
	.polyMode = VK_POLYGON_MODE_FILL,
	.cullFlags = VK_CULL_MODE_BACK_BIT,
	.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
	.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
	.dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
	.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
	.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
	.extraPrimitiveOverestimationSize = 0.0f,
	.conservativeRasterEnable = false,
	.depthWrite = true,
	.depthTestEnable = true,
	.blendCol = colorBlending,
};

using vk_shader_stage_list = std::initializer_list<VkPipelineShaderStageCreateInfo>;

struct vk_render_attachment
{
	VkFormat format;
};

// TODO: rework
// TODO: specialization for gfx ?
// TODO: depth clamp ?
// TODO: entry point name
VkPipeline VkMakeGfxPipeline(
	VkDevice			vkDevice,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		vs,
	VkShaderModule		fs,
	const VkFormat* desiredColorFormat,
    VkFormat           desiredDepthFormat,
	const vk_gfx_pipeline_state& pipelineState,
	const char* name
) {
	VkPipelineShaderStageCreateInfo shaderStagesInfo[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vs,
			.pName = SHADER_ENTRY_POINT,
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fs,
			.pName = SHADER_ENTRY_POINT,
		}

	};

	u32 shaderStagesCount = bool( vs ) + bool( fs );

	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = pipelineState.primTopology
	};
	VkPipelineViewportStateCreateInfo viewportInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1
	};

	constexpr VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = ( u32 ) std::size( dynamicStates ),
		.pDynamicStates = dynamicStates
	};

	VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterState = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT,
		.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT,
		.extraPrimitiveOverestimationSize = pipelineState.extraPrimitiveOverestimationSize
	};

	VkPipelineRasterizationStateCreateInfo rasterInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = pipelineState.conservativeRasterEnable ? &conservativeRasterState : 0,
		.depthClampEnable = 0,
		.rasterizerDiscardEnable = 0,
		.polygonMode = pipelineState.polyMode,
		.cullMode = pipelineState.cullFlags,
		.frontFace = pipelineState.frontFace,
		.lineWidth = 1.0f
	};

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, 
		.depthTestEnable = pipelineState.depthTestEnable,
		.depthWriteEnable = pipelineState.depthWrite,
		.depthCompareOp = VK_COMPARE_OP_GREATER,
		.depthBoundsTestEnable = VK_TRUE,
		.minDepthBounds = 0.0f,
		.maxDepthBounds = 1.0f
	};

	constexpr VkColorComponentFlags colWriteMask = 
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendAttachmentState blendConfig = {
		.blendEnable = pipelineState.blendCol,
		.srcColorBlendFactor = pipelineState.srcColorBlendFactor,
		.dstColorBlendFactor = pipelineState.dstColorBlendFactor,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = pipelineState.srcAlphaBlendFactor,
		.dstAlphaBlendFactor = pipelineState.dstAlphaBlendFactor,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = colWriteMask
	};
	
	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendConfig
	};

	// TODO: only if we use frag
	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};
	
	VkPipelineRenderingCreateInfo renderingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = desiredColorFormat ? 1u : 0u,
		.pColorAttachmentFormats = desiredColorFormat,
		.depthAttachmentFormat = desiredDepthFormat
	};

	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkGraphicsPipelineCreateInfo pipelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingInfo,
		.stageCount = shaderStagesCount,
		.pStages = shaderStagesInfo,
		.pVertexInputState = &vtxInCreateInfo,
		.pInputAssemblyState = &inAsmStateInfo,
		.pViewportState = &viewportInfo,
		.pRasterizationState = &rasterInfo,
		.pMultisampleState = &multisamplingInfo,
		.pDepthStencilState = &depthStencilState,
		.pColorBlendState = &colorBlendStateInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = vkPipelineLayout,
		.basePipelineIndex = -1
	};

	VkPipeline vkGfxPipeline;
	VK_CHECK( vkCreateGraphicsPipelines( vkDevice, 0, 1, &pipelineInfo, 0, &vkGfxPipeline ) );

	VkDbgNameObj( vkGfxPipeline, vkDevice, name );

	return vkGfxPipeline;
}