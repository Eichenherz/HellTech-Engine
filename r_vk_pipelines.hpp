#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
// TODO: autogen custom vulkan ?
#include <vulkan.h>
// TODO: header + .cpp ?
// TODO: revisit this
#include "vk_procs.h"

#include "sys_os_api.h"

#include "vk_utils.hpp"

#include <vector>

static bool colorBlending = 0;

// TODO: variable entry point
constexpr char SHADER_ENTRY_POINT[] = "main";

// TODO: store more stuff ?
struct vk_gfx_pipeline_state
{
	VkPolygonMode		polyMode = VK_POLYGON_MODE_FILL;
	VkCullModeFlags		cullFlags = VK_CULL_MODE_BACK_BIT;
	VkFrontFace			frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	VkPrimitiveTopology primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkBlendFactor       srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	VkBlendFactor       dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	VkBlendFactor       srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	VkBlendFactor       dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	float               extraPrimitiveOverestimationSize = 0.0f;
	bool                conservativeRasterEnable = false;
	bool				depthWrite = true;
	bool				depthTestEnable = true;
	bool				blendCol = colorBlending;
};


using vk_shader_stage_list = std::initializer_list<VkPipelineShaderStageCreateInfo>;
using vk_specializations = std::initializer_list<u32>;

inline VkSpecializationInfo VkMakeSpecializationInfo( std::vector<VkSpecializationMapEntry>& specializations, const vk_specializations& consts )
{
	specializations.resize( std::size( consts ) );
	u64 sizeOfASpecConst = sizeof( *std::cbegin( consts ) );
	for( u64 i = 0; i < std::size( consts ); ++i )
	{
		specializations[ i ] = { u32( i ), u32( i * sizeOfASpecConst ), u32( sizeOfASpecConst ) };
	}
	
	return {
		.mapEntryCount = (u32) std::size( specializations ),
		.pMapEntries = std::data( specializations ),
		.dataSize = std::size( consts ) * sizeOfASpecConst,
		.pData = std::cbegin( consts )
	};
}

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

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = std::size( dynamicStates ),
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
		.colorAttachmentCount = desiredColorFormat ? 1 : 0,
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


VkPipeline VkMakeComputePipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		cs,
	vk_specializations	consts,
	const char* name,
	const char* pEntryPointName = SHADER_ENTRY_POINT
) {
	std::vector<VkSpecializationMapEntry> specializations;
	VkSpecializationInfo specInfo = VkMakeSpecializationInfo( specializations, consts );

	VkPipelineShaderStageCreateInfo stage = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = cs,
	    .pName = pEntryPointName,
	    .pSpecializationInfo = &specInfo,
	};
	
	VkComputePipelineCreateInfo compPipelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = stage,
		.layout = vkPipelineLayout
	};

	VkPipeline pipeline = 0;
	VK_CHECK( vkCreateComputePipelines( vkDevice, vkPipelineCache, 1, &compPipelineInfo, 0, &pipeline ) );

	VkDbgNameObj( pipeline, vkDevice, name );

	return pipeline;
}
