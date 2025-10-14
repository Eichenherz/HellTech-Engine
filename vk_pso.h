#ifndef __VK_PSO_H__
#define __VK_PSO_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
#include "vk_procs.h"

#include "vk_error.h"
#include "core_types.h"
#include <span>

#include <SPIRV-Reflect/spirv_reflect.h>

inline spv_reflect::ShaderModule SpvMakeReflectedShaderModule( const std::span<u8> spvByteCode )
{
	spv_reflect::ShaderModule reflInfo = { 
		std::size( spvByteCode ), std::data( spvByteCode ), SPV_REFLECT_MODULE_FLAG_NO_COPY };
	VK_CHECK( ( VkResult ) reflInfo.GetResult() );
	assert( 1 == reflInfo.GetEntryPointCount() );
	return reflInfo;
}

// TODO: variable entry point
constexpr char SHADER_ENTRY_POINT[] = "main";

inline static VkShaderModule VkMakeShaderModule( VkDevice vkDevice, const u32* spv, u64 size )
{
	VkShaderModuleCreateInfo shaderModuleInfo = { 
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = spv,
	};

	VkShaderModule sm;
	VK_CHECK( vkCreateShaderModule( vkDevice, &shaderModuleInfo, 0, &sm ) );
	return sm;
}

struct vk_shader2
{
	VkShaderModule        shaderModule;
	char                  entryPoint[ 128 ];
	VkShaderStageFlagBits stage;

	~vk_shader2()
	{
		//vkDestroyShaderModule( vk.pDc->device, shaderModule, 0 );
	}
};

inline static vk_shader2 VkLoadShader2( const char* shaderPath, VkDevice vkDevice )
{
	std::vector<u8> spvByteCode = SysReadFile( shaderPath );

	spv_reflect::ShaderModule reflInfo = SpvMakeReflectedShaderModule( spvByteCode );
	const char* pEntryPointName = reflInfo.GetEntryPointName();
	VkShaderStageFlagBits shaderStage = ( VkShaderStageFlagBits ) reflInfo.GetShaderStage();
	VkShaderModule sm = VkMakeShaderModule( vkDevice, ( const u32* ) std::data( spvByteCode ), std::size( spvByteCode ) );

	vk_shader2 shader = { .shaderModule = sm, .stage = shaderStage };

	assert( sizeof( shader.entryPoint ) >= std::strlen( pEntryPointName + 1 ) );
	strcpy_s( shader.entryPoint, sizeof( shader.entryPoint ), pEntryPointName );
	assert( std::strlen( shader.entryPoint ) == std::strlen( pEntryPointName ) );

	return shader;
}

inline VkPipelineShaderStageCreateInfo VkMakePipelineShaderInfo( const vk_shader2& shader )
{
	return {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = shader.stage,
		.module = shader.shaderModule,
		.pName = shader.entryPoint,
	};
}

struct vk_shader
{
	std::vector<u8>	spvByteCode;
	VkShaderModule	module;

	// TODO: remove
	u64				timestamp;
	char			entryPointName[ 32 ];
};

struct group_size
{
	u32 x : 8;
	u32 y : 8;
	u32 z : 8;
};


inline static vk_shader VkLoadShader( const char* shaderPath, VkDevice vkDevice )
{
	constexpr std::string_view shadersFolder = "Shaders/";
	constexpr std::string_view shaderExtension = ".spv";

	std::vector<u8> binSpvShader = SysReadFile( shaderPath );

	vk_shader shader = {};
	shader.spvByteCode = std::move( binSpvShader );
	shader.module = VkMakeShaderModule( vkDevice, 
										(const u32*) std::data( shader.spvByteCode ), 
										std::size( shader.spvByteCode ) );

	std::string_view shaderName = { shaderPath };
	shaderName.remove_prefix( std::size( shadersFolder ) );
	shaderName.remove_suffix( std::size( shaderExtension ) - 1 );
	VkDbgNameObj( shader.module, vkDevice, &shaderName[ 0 ] );

	return shader;
}

// TODO: map spec consts ?
using vk_shader_list = std::initializer_list<vk_shader*>;
using vk_specializations = std::initializer_list<u32>;

inline static VkSpecializationInfo
VkMakeSpecializationInfo(
	std::vector<VkSpecializationMapEntry>& specializations,
	const vk_specializations& consts
) {
	constexpr u64 sizeOfASpecConst = sizeof( std::decay_t<decltype( consts )>::value_type );

	specializations.resize( std::size( consts ) );
	for( u64 i = 0; i < std::size( consts ); ++i )
	{
		specializations[ i ] = { u32( i ), u32( i * sizeOfASpecConst ), u32( sizeOfASpecConst ) };
	}

	VkSpecializationInfo specInfo = {
		.mapEntryCount = ( u32 ) std::size( specializations ),
		.pMapEntries = std::data( specializations ),
		.dataSize = std::size( consts ) * sizeOfASpecConst,
		.pData = std::cbegin( consts )
	};

	return specInfo;
}

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
	bool				blendCol = false;
};

// VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
VkPipeline VkMakeGfxPipeline(
	VkDevice			                       vkDevice,
	std::span<VkPipelineShaderStageCreateInfo> shaderStagesInfo,
	std::span<VkDynamicState>                  dynamicStates,
	const VkFormat*                            pColorAttachmentFormats,
	u32                                        colorAttachmentCount,
	VkFormat                                   depthAttachmentFormat,
	VkPipelineLayout	                       vkPipelineLayout,
	const vk_gfx_pipeline_state&               pipelineState
) {
	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = pipelineState.primTopology,
	};

	VkPipelineViewportStateCreateInfo viewportInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = ( u32 ) std::size( dynamicStates ),
		.pDynamicStates = std::data( dynamicStates ),
	};

	VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterState = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT,
		.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT,
		.extraPrimitiveOverestimationSize = pipelineState.extraPrimitiveOverestimationSize,
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
		.minDepthBounds = 0,
		.maxDepthBounds = 1.0f
	};

	VkPipelineColorBlendAttachmentState blendConfig = {
		.blendEnable = pipelineState.blendCol,
		.srcColorBlendFactor = pipelineState.srcColorBlendFactor,
		.dstColorBlendFactor = pipelineState.dstColorBlendFactor,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = pipelineState.srcAlphaBlendFactor,
		.dstAlphaBlendFactor = pipelineState.dstAlphaBlendFactor,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendConfig,
	};

	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineRenderingCreateInfo renderingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = colorAttachmentCount,
		.pColorAttachmentFormats = pColorAttachmentFormats,
		.depthAttachmentFormat = depthAttachmentFormat,
	};

	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingInfo,
		.stageCount = ( u32 ) std::size( shaderStagesInfo ),
		.pStages = std::data( shaderStagesInfo ),
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

	return vkGfxPipeline;
}

// TODO: shader stages more general
// TODO: specialization for gfx ?
// TODO: depth clamp ?
// TODO: entry point name
//[[deprecated]]
VkPipeline VkMakeGfxPipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		vs,
	VkShaderModule		fs,
	const VkFormat* pColorAttachmentFormats,
	u32 colorAttachmentCount,
	VkFormat depthAttachmentFormat,
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

	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = pipelineState.primTopology,
	};

	VkPipelineViewportStateCreateInfo viewportInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = std::size( dynamicStates ),
		.pDynamicStates = dynamicStates,
	};

	// TODO: place inside if ?
	VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterState = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT,
		.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT,
		.extraPrimitiveOverestimationSize = pipelineState.extraPrimitiveOverestimationSize,
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
		.minDepthBounds = 0,
		.maxDepthBounds = 1.0f
	};

	VkPipelineColorBlendAttachmentState blendConfig = {
		.blendEnable = pipelineState.blendCol,
		.srcColorBlendFactor = pipelineState.srcColorBlendFactor,
		.dstColorBlendFactor = pipelineState.dstColorBlendFactor,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = pipelineState.srcAlphaBlendFactor,
		.dstAlphaBlendFactor = pipelineState.dstAlphaBlendFactor,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendConfig,
	};
	// TODO: only if we use frag
	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineRenderingCreateInfo renderingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = colorAttachmentCount,
		.pColorAttachmentFormats = pColorAttachmentFormats,
		.depthAttachmentFormat = depthAttachmentFormat,
	};

	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkGraphicsPipelineCreateInfo pipelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingInfo,
		.stageCount = shaderStagesCount,// std::size( shaderStagesInfo ),
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
	VK_CHECK( vkCreateGraphicsPipelines( vkDevice, vkPipelineCache, 1, &pipelineInfo, 0, &vkGfxPipeline ) );

	return vkGfxPipeline;
}


VkPipeline VkMakeComputePipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		cs,
	vk_specializations	consts,
	u32                 waveLaneCount,
	const char*			pEntryPointName = SHADER_ENTRY_POINT,
	const char*         pName = ""
){
	std::vector<VkSpecializationMapEntry> specializations;
	VkSpecializationInfo specInfo = VkMakeSpecializationInfo( specializations, consts );

	VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroupSizeInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
		.requiredSubgroupSize = waveLaneCount
	};

	VkPipelineShaderStageCreateInfo stage = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = &subgroupSizeInfo,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = cs,
		.pName = pEntryPointName,
		.pSpecializationInfo = &specInfo
	};

	VkComputePipelineCreateInfo compPipelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = stage,
		.layout = vkPipelineLayout
	};

	VkPipeline pipeline;
	VK_CHECK( vkCreateComputePipelines( vkDevice, vkPipelineCache, 1, &compPipelineInfo, 0, &pipeline ) );
	if(pName) VkDbgNameObj( pipeline, vkDevice, pName );

	return pipeline;
}

// TODO: rename to program 
// TODO: what about descriptors ?
// TODO: what about spec consts
struct vk_graphics_program
{
	VkPipeline pipeline;
	VkPipelineLayout layout;
	// TODO: store push consts data ?
};

struct vk_compute_program
{
	VkPipeline pipeline;
	VkPipelineLayout layout;
	u16 workgrX;
	u16 workgrY;
	u16 workgrZ;
};

#endif // !__VK_PSO_H__
