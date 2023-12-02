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

#include <string_view>
#include <assert.h>
#include <vector>


#include "sys_os_api.h"
#include "core_lib_api.h"

#include "vk_utils.hpp"

// TODO: remove
#define WIN32
#include "spirv_reflect.h"
#undef WIN32

// TODO: rethink 
// TODO: cache shader ?
struct vk_shader
{
	VkShaderModule	module;
	std::vector<u8>	spvByteCode;
	u64				timestamp;
	char			entryPointName[ 32 ];
};

// TODO: where to place this ?
struct group_size
{
	u32 x;
	u32 y;
	u32 z;
};

// TODO: should make obsolete
// TODO: vk_shader_program ?
// TODO: put shader module inside ?
struct vk_program
{
	VkPipelineLayout			pipeLayout;
	VkDescriptorUpdateTemplate	descUpdateTemplate;
	VkShaderStageFlags			pushConstStages;
	VkDescriptorSetLayout       descSetLayout;
	group_size					groupSize;
};


inline static VkShaderModule VkMakeShaderModule( VkDevice vkDevice, const u32* spv, u64 size )
{
	VkShaderModuleCreateInfo shaderModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	shaderModuleInfo.codeSize = size;
	shaderModuleInfo.pCode = spv;

	VkShaderModule sm = {};
	VK_CHECK( vkCreateShaderModule( vkDevice, &shaderModuleInfo, 0, &sm ) );
	return sm;
}

inline static vk_shader VkLoadShader( const char* shaderPath, VkDevice vkDevice )
{
	// TODO: 
	using namespace std;
	constexpr std::string_view shadersFolder = "Shaders/"sv;
	constexpr std::string_view shaderExtension = ".spv"sv;

	std::vector<u8> binSpvShader = SysReadFile( shaderPath );

	vk_shader shader = {};
	shader.spvByteCode = std::move( binSpvShader );
	shader.module = VkMakeShaderModule( vkDevice,
										( const u32* ) std::data( shader.spvByteCode ),
										std::size( shader.spvByteCode ) );

	std::string_view shaderName = { shaderPath };
	shaderName.remove_prefix( std::size( shadersFolder ) );
	shaderName.remove_suffix( std::size( shaderExtension ) - 1 );
	VkDbgNameObj( shader.module, vkDevice, &shaderName[ 0 ] );

	return shader;
}

inline static void VkReflectShaderLayout(
	const VkPhysicalDeviceProperties& gpuProps,
	const std::vector<u8>& spvByteCode,
	std::vector<VkDescriptorSetLayoutBinding>& descSetBindings,
	std::vector<VkPushConstantRange>& pushConstRanges,
	group_size& gs,
	char* entryPointName,
	u64											entryPointNameStrLen
) {
	SpvReflectShaderModule shaderReflection;
	VK_CHECK( ( VkResult ) spvReflectCreateShaderModule( std::size( spvByteCode ) * sizeof( spvByteCode[ 0 ] ),
														 std::data( spvByteCode ),
														 &shaderReflection ) );

	SpvReflectDescriptorSet& set = shaderReflection.descriptor_sets[ 0 ];

	for( u64 bindingIdx = 0; bindingIdx < set.binding_count; ++bindingIdx )
	{
		if( set.set > 0 ) continue;

		const SpvReflectDescriptorBinding& descBinding = *set.bindings[ bindingIdx ];

		if( bindingIdx < std::size( descSetBindings ) )
		{
			// NOTE: if binding matches, assume the same resource will be used in multiple shaders in the same pipeline/program
			// TODO: should VK_CHECK here ?
			assert( descSetBindings[ bindingIdx ].descriptorType == VkDescriptorType( descBinding.descriptor_type ) );
			descSetBindings[ bindingIdx ].stageFlags |= shaderReflection.shader_stage;
			continue;
		}

		VkDescriptorSetLayoutBinding binding = {};
		binding.binding = descBinding.binding;
		binding.descriptorType = VkDescriptorType( descBinding.descriptor_type );
		binding.descriptorCount = descBinding.count;
		binding.stageFlags = shaderReflection.shader_stage;
		descSetBindings.push_back( binding );
	}

	for( u64 pci = 0; pci < shaderReflection.push_constant_block_count; ++pci )
	{
		VkPushConstantRange pushConstRange = {};
		pushConstRange.stageFlags = shaderReflection.shader_stage;
		pushConstRange.offset = shaderReflection.push_constant_blocks[ pci ].offset;
		pushConstRange.size = shaderReflection.push_constant_blocks[ pci ].size;
		VK_CHECK( VK_INTERNAL_ERROR( pushConstRange.size > gpuProps.limits.maxPushConstantsSize ) );

		pushConstRanges.push_back( pushConstRange );
	}

	assert( shaderReflection.entry_point_count == 1 );
	const SpvReflectEntryPoint& entryPoint = shaderReflection.entry_points[ 0 ];
	assert( std::strlen( entryPoint.name ) <= entryPointNameStrLen );
	std::memcpy( entryPointName, entryPoint.name, entryPointNameStrLen );
	if( VkShaderStageFlags( shaderReflection.shader_stage ) == VK_SHADER_STAGE_COMPUTE_BIT )
		gs = { entryPoint.local_size.x, entryPoint.local_size.y, entryPoint.local_size.z };

	spvReflectDestroyShaderModule( &shaderReflection );
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

// TODO: map spec consts ?
using vk_shader_list = std::initializer_list<vk_shader*>;
using vk_dynamic_states = std::initializer_list<VkDynamicState>;

// TODO: bindlessLayout only for the shaders that use it ?
inline static vk_program VkMakePipelineProgram(
	VkDevice							vkDevice,
	const VkPhysicalDeviceProperties& gpuProps,
	VkPipelineBindPoint					bindPoint,
	vk_shader_list						shaders,
	VkDescriptorSetLayout				bindlessLayout
) {
	assert( std::size( shaders ) );

	vk_program program = {};

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkPushConstantRange>	pushConstRanges;
	group_size gs = {};

	for( vk_shader* s : shaders )
	{
		VkReflectShaderLayout(
			gpuProps, s->spvByteCode, bindings, pushConstRanges, gs, s->entryPointName, std::size( s->entryPointName ) );
	}

	VkDescriptorSetLayout descSetLayout = {};
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	descSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	descSetLayoutInfo.bindingCount = std::size( bindings );
	descSetLayoutInfo.pBindings = std::data( bindings );
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &descSetLayout ) );

	VkDescriptorSetLayout setLayouts[] = { descSetLayout, bindlessLayout };

	VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipeLayoutInfo.setLayoutCount = std::size( setLayouts );
	pipeLayoutInfo.pSetLayouts = setLayouts;
	pipeLayoutInfo.pushConstantRangeCount = std::size( pushConstRanges );
	pipeLayoutInfo.pPushConstantRanges = std::data( pushConstRanges );
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipeLayoutInfo, 0, &program.pipeLayout ) );

	if( std::size( bindings ) )
	{
		std::vector<VkDescriptorUpdateTemplateEntry> entries;
		entries.reserve( std::size( bindings ) );
		for( const VkDescriptorSetLayoutBinding& binding : bindings )
		{
			VkDescriptorUpdateTemplateEntry entry = {};
			entry.dstBinding = binding.binding;
			entry.dstArrayElement = 0;
			entry.descriptorCount = binding.descriptorCount;
			entry.descriptorType = binding.descriptorType;
			entry.offset = std::size( entries ) * sizeof( vk_descriptor_info );
			entry.stride = sizeof( vk_descriptor_info );
			entries.emplace_back( entry );
		}

		VkDescriptorUpdateTemplateCreateInfo templateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO };
		templateInfo.descriptorUpdateEntryCount = std::size( entries );
		templateInfo.pDescriptorUpdateEntries = std::data( entries );
		templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
		templateInfo.descriptorSetLayout = descSetLayout;
		templateInfo.pipelineBindPoint = bindPoint;
		templateInfo.pipelineLayout = program.pipeLayout;
		templateInfo.set = 0;
		VK_CHECK( vkCreateDescriptorUpdateTemplate( vkDevice, &templateInfo, 0, &program.descUpdateTemplate ) );
	}

	//vkDestroyDescriptorSetLayout( vkDevice, descSetLayout, 0 );

	program.descSetLayout = descSetLayout;
	program.pushConstStages = std::size( pushConstRanges ) ? pushConstRanges[ 0 ].stageFlags : 0;
	program.groupSize = gs;

	return program;
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