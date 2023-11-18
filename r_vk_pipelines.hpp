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

inline static VkSpecializationInfo
VkMakeSpecializationInfo(
	std::vector<VkSpecializationMapEntry>& specializations,
	const vk_specializations& consts
) {
	specializations.resize( std::size( consts ) );
	u64 sizeOfASpecConst = sizeof( *std::cbegin( consts ) );
	for( u64 i = 0; i < std::size( consts ); ++i )
		specializations[ i ] = { u32( i ), u32( i * sizeOfASpecConst ), u32( sizeOfASpecConst ) };

	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = std::size( specializations );
	specInfo.pMapEntries = std::data( specializations );
	specInfo.dataSize = std::size( consts ) * sizeOfASpecConst;
	specInfo.pData = std::cbegin( consts );

	return specInfo;
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
	const vk_gfx_pipeline_state& pipelineState
) {
	VkPipelineShaderStageCreateInfo shaderStagesInfo[ 2 ] = {};
	shaderStagesInfo[ 0 ].sType = shaderStagesInfo[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

	shaderStagesInfo[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStagesInfo[ 0 ].module = vs;
	shaderStagesInfo[ 0 ].pName = SHADER_ENTRY_POINT;
	shaderStagesInfo[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStagesInfo[ 1 ].module = fs;
	shaderStagesInfo[ 1 ].pName = SHADER_ENTRY_POINT;

	u32 shaderStagesCount = bool( vs ) + bool( fs );

	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inAsmStateInfo.topology = pipelineState.primTopology;

	VkPipelineViewportStateCreateInfo viewportInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportInfo.viewportCount = 1;
	viewportInfo.scissorCount = 1;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateInfo.dynamicStateCount = std::size( dynamicStates );
	dynamicStateInfo.pDynamicStates = dynamicStates;

	// TODO: place inside if ?
	VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterState =
	{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT };
	conservativeRasterState.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
	conservativeRasterState.extraPrimitiveOverestimationSize = pipelineState.extraPrimitiveOverestimationSize;

	VkPipelineRasterizationStateCreateInfo rasterInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterInfo.pNext = pipelineState.conservativeRasterEnable ? &conservativeRasterState : 0;
	rasterInfo.depthClampEnable = 0;
	rasterInfo.rasterizerDiscardEnable = 0;
	rasterInfo.polygonMode = pipelineState.polyMode;
	rasterInfo.cullMode = pipelineState.cullFlags;
	rasterInfo.frontFace = pipelineState.frontFace;
	rasterInfo.lineWidth = 1.0f;

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
	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;


	VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	renderingInfo.colorAttachmentCount = desiredColorFormat ? 1 : 0;
	renderingInfo.pColorAttachmentFormats = desiredColorFormat;
	renderingInfo.depthAttachmentFormat = desiredDepthFormat;


	VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = shaderStagesCount;
	pipelineInfo.pStages = shaderStagesInfo;
	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	pipelineInfo.pVertexInputState = &vtxInCreateInfo;
	pipelineInfo.pInputAssemblyState = &inAsmStateInfo;
	pipelineInfo.pViewportState = &viewportInfo;
	pipelineInfo.pRasterizationState = &rasterInfo;
	pipelineInfo.pMultisampleState = &multisamplingInfo;
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = vkPipelineLayout;
	pipelineInfo.basePipelineIndex = -1;

	VkPipeline vkGfxPipeline;
	VK_CHECK( vkCreateGraphicsPipelines( vkDevice, 0, 1, &pipelineInfo, 0, &vkGfxPipeline ) );

	return vkGfxPipeline;
}

// TODO: pipeline caputre representations blah blah ?
VkPipeline VkMakeComputePipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		cs,
	vk_specializations	consts,
	const char* pEntryPointName = SHADER_ENTRY_POINT
) {
	std::vector<VkSpecializationMapEntry> specializations;
	VkSpecializationInfo specInfo = VkMakeSpecializationInfo( specializations, consts );

	VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = cs;
	stage.pName = pEntryPointName;
	stage.pSpecializationInfo = &specInfo;

	VkComputePipelineCreateInfo compPipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	compPipelineInfo.stage = stage;
	compPipelineInfo.layout = vkPipelineLayout;

	VkPipeline pipeline = 0;
	VK_CHECK( vkCreateComputePipelines( vkDevice, vkPipelineCache, 1, &compPipelineInfo, 0, &pipeline ) );

	return pipeline;
}
