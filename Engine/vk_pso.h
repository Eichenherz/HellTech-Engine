#ifndef __VK_PSO_H__
#define __VK_PSO_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "ht_error.h"
#include "vk_error.h"
#include "ht_core_types.h"

#include <span>
#include "ht_fixed_string.h"
#include <SPIRV-Reflect/spirv_reflect.h>

inline spv_reflect::ShaderModule SpvMakeReflectedShaderModule( std::span<const u8> spvByteCode )
{
	spv_reflect::ShaderModule reflInfo = { 
		std::size( spvByteCode ), std::data( spvByteCode ), SPV_REFLECT_MODULE_FLAG_NO_COPY };
	VK_CHECK( ( VkResult ) reflInfo.GetResult() );
	HT_ASSERT( 1 == reflInfo.GetEntryPointCount() );
	return reflInfo;
}

struct vk_shader
{
	fixed_string<128>    entryPoint;
	VkShaderModule        module;
	VkShaderStageFlagBits stage;
};

struct vk_gfx_shader_stage : VkPipelineShaderStageCreateInfo
{
	inline vk_gfx_shader_stage( const vk_shader& shader ) : VkPipelineShaderStageCreateInfo{}
	{
		this->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		this->stage = shader.stage;
		this->module = shader.module;
		this->pName = std::data( shader.entryPoint );
	}
};

struct group_size
{
	u32 x : 8;
	u32 y : 8;
	u32 z : 8;
};

struct vk_gfx_pso_config
{
	VkPolygonMode		polyMode;
	VkCullModeFlags		cullFlags;
	VkFrontFace			frontFace;
	VkPrimitiveTopology primTopology;
	VkBlendFactor       srcColorBlendFactor;
	VkBlendFactor       dstColorBlendFactor;
	VkBlendOp           colorBlendOp;
	VkBlendFactor       srcAlphaBlendFactor;
	VkBlendFactor       dstAlphaBlendFactor;
	VkBlendOp           alphaBlendOp;
	bool				depthWrite;
	bool				depthTestEnable;
	bool				blendCol;
};

constexpr vk_gfx_pso_config DEFAULT_PSO = {
	.polyMode = VK_POLYGON_MODE_FILL,
	.cullFlags = VK_CULL_MODE_BACK_BIT,
	.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
	.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
	.dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
	.colorBlendOp = VK_BLEND_OP_ADD,
	.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
	.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
	.alphaBlendOp = VK_BLEND_OP_ADD,
	.depthWrite = true,
	.depthTestEnable = true,
	.blendCol = false
};

#endif // !__VK_PSO_H__
