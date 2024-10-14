#ifndef __VK_SHADER__
#define __VK_SHADER__

#include "vk_common.hpp"

#include <string_view>
#include <assert.h>
#include <vector>


#include "sys_os_api.h"

#include "vk_utils.hpp"

using vk_specialization_type = u32;

struct vk_shader_metadata
{
	std::vector<u8>	spvByteCode;
	const char* entryPointName;
	VkShaderStageFlags stage;
};

struct vk_shader
{
	VkShaderModule hndl;
	const char* entryPointName;
	VkShaderStageFlags stage;
};

inline std::vector<VkSpecializationMapEntry> VkMakeSpecializationMap( const std::span<vk_specialization_type> consts )
{
	std::vector<VkSpecializationMapEntry> specializations;
	specializations.resize( std::size( consts ) );

	u64 sizeOfASpecConst = sizeof( decltype( consts )::value_type );
	for( u64 i = 0; i < std::size( consts ); ++i )
	{
		specializations[ i ] = { u32( i ), u32( i * sizeOfASpecConst ), sizeOfASpecConst };
	}
	return specializations;
}

inline static VkShaderModule VkMakeShaderModule( VkDevice vkDevice, const u32* spv, u64 size, const char* name )
{
	VkShaderModuleCreateInfo shaderModuleInfo = { 
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = spv
	};

	VkShaderModule sm;
	VK_CHECK( vkCreateShaderModule( vkDevice, &shaderModuleInfo, 0, &sm ) );
	return sm;
}

struct vk_descriptor_info
{
	union
	{
		VkDescriptorBufferInfo buff;
		VkDescriptorImageInfo img;
	};

	vk_descriptor_info() = default;

	vk_descriptor_info( VkBuffer buff, u64 offset, u64 range ) : buff{ buff, offset, range } {}
	vk_descriptor_info( VkDescriptorBufferInfo buffInfo ) : buff{ buffInfo } {}
	vk_descriptor_info( VkSampler sampler, VkImageView view, VkImageLayout imgLayout ) : img{ sampler, view, imgLayout } {}
	vk_descriptor_info( VkDescriptorImageInfo imgInfo ) : img{ imgInfo } {}
};

using vk_dynamic_states = std::initializer_list<VkDynamicState>;

#endif // !__VK_SHADER__
