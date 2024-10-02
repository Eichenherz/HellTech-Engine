#ifndef __VK_SHADER__
#define __VK_SHADER__

#include "vk_common.hpp"

#include <string_view>
#include <assert.h>
#include <vector>


#include "sys_os_api.h"
#include "core_lib_api.h"

#include "vk_utils.hpp"

struct vk_shader_metadata
{
	std::vector<u8>	spvByteCode;
	const char* entryPointName;
	VkShaderStageFlags stage;
};

std::vector<vk_specialization_type>	specConsts;

struct vk_shader
{
	VkShaderModule hndl;
	const char* entryPointName;
	VkShaderStageFlags stage;
};

using vk_specialization_type = u32;

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

	VkDbgNameObj( sm, vkDevice, name );
	return sm;
}

inline static vk_shader VkLoadShader( const char* shaderPath, VkDevice vkDevice )
{
	// TODO: 
	using namespace std;
	constexpr std::string_view shadersFolder = "Shaders/"sv;
	constexpr std::string_view shaderExtension = ".spv"sv;

	std::vector<u8> binSpvShader = SysReadFile( shaderPath );

	std::string_view shaderName = { shaderPath };
	shaderName.remove_prefix( std::size( shadersFolder ) );
	shaderName.remove_suffix( std::size( shaderExtension ) - 1 );

	vk_shader shader = {};
	shader.spvByteCode = std::move( binSpvShader );
	shader.module = VkMakeShaderModule( 
		vkDevice, ( const u32* ) std::data( shader.spvByteCode ), std::size( shader.spvByteCode ), &shaderName[ 0 ] );

	return shader;
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
