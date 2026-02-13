#ifndef __VK_PSO_H__
#define __VK_PSO_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>

#include <Volk/volk.h>

#include "vk_error.h"
#include "core_types.h"
#include <span>

#include <functional>

#include <SPIRV-Reflect/spirv_reflect.h>

inline spv_reflect::ShaderModule SpvMakeReflectedShaderModule( std::span<const u8> spvByteCode )
{
	spv_reflect::ShaderModule reflInfo = { 
		std::size( spvByteCode ), std::data( spvByteCode ), SPV_REFLECT_MODULE_FLAG_NO_COPY };
	VK_CHECK( ( VkResult ) reflInfo.GetResult() );
	HT_ASSERT( 1 == reflInfo.GetEntryPointCount() );
	return reflInfo;
}

// TODO: variable entry point
constexpr char SHADER_ENTRY_POINT[] = "main";

struct vk_shader
{
	std::string           entryPoint;
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
		this->pName = shader.entryPoint.c_str();
	}
};

struct group_size
{
	u32 x : 8;
	u32 y : 8;
	u32 z : 8;
};


// TODO: map spec consts ?
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
	bool				depthWrite = true;
	bool				depthTestEnable = true;
	bool				blendCol = false;
};

#endif // !__VK_PSO_H__
