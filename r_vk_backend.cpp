#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
#include "vk_procs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <string_view>
#include <charconv>
#include <span>
#include <format>
#include <memory>

#include "vk_resources.h"
#include "vk_descriptor.h"
#include "vk_timer.h"
#include "vk_error.h"

// NOTE: clang-cl on VS issue
#ifdef __clang__
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#endif

#include <DirectXPackedVector.h>

namespace DXPacked = DirectX::PackedVector;

#include "sys_os_api.h"


//====================CONSTS====================//
constexpr u64 VK_MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;

constexpr u32 NOT_USED_IDX = -1;
constexpr u32 OBJ_CULL_WORKSIZE = 64;
constexpr u32 MLET_CULL_WORKSIZE = 256;
//==============================================//
// TODO: cvars
//====================CVARS====================//

//==============================================//
// TODO: compile time switches
//==============CONSTEXPR_SWITCH==============//

//==============================================//


// TODO: remove ?
static const DXPacked::XMCOLOR white = { 255u, 255u, 255u, 1 };
static const DXPacked::XMCOLOR black = { 0u, 0u, 0u, 1 };
static const DXPacked::XMCOLOR gray = { 0x80u, 0x80u, 0x80u, 1 };
static const DXPacked::XMCOLOR lightGray = { 0xD3u, 0xD3u, 0xD3u, 1 };
static const DXPacked::XMCOLOR red = { 255u, 0u, 0u, 1 };
static const DXPacked::XMCOLOR green = { 0u, 255u, 0u, 1 };
static const DXPacked::XMCOLOR blue = { 0u, 0u, 255u, 1 };
static const DXPacked::XMCOLOR yellow = { 255u, 255u, 0u, 1 };
static const DXPacked::XMCOLOR cyan = { 0u, 255u, 255u, 1 };
static const DXPacked::XMCOLOR magenta = { 255u, 0u, 255u, 1 };


struct vk_scoped_label
{
	VkCommandBuffer cmdBuff;

	inline vk_scoped_label( VkCommandBuffer _cmdBuff, const char* labelName, DXPacked::XMCOLOR col )
	{
		this->cmdBuff = _cmdBuff;
		
		VkDebugUtilsLabelEXT dbgLabel = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
			.pLabelName = labelName,
			.color = { ( float ) col.r, ( float ) col.g, ( float ) col.b, ( float ) col.a },
		};
		vkCmdBeginDebugUtilsLabelEXT( cmdBuff, &dbgLabel );
	}
	inline ~vk_scoped_label()
	{
		vkCmdEndDebugUtilsLabelEXT( cmdBuff );
	}
};


constexpr VkValidationFeatureEnableEXT enabledValidationFeats[] = {
		//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
		VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
};

struct vk_instance
{
	u64 dll;
	VkInstance hndl;
	VkDebugUtilsMessengerEXT dbgMsg;
};

inline static vk_instance VkMakeInstance()
{
	constexpr const char* ENABLED_INST_EXTS[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
	#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	#endif // VK_USE_PLATFORM_WIN32_KHR
	#ifdef _VK_DEBUG_
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	#endif // _VK_DEBUG_
	};

	constexpr const char* LAYERS[] =
	{
	#ifdef _VK_DEBUG_
		"VK_LAYER_KHRONOS_validation",
		//"VK_LAYER_LUNARG_api_dump"
	#endif // _VK_DEBUG_
	};

	u64 VK_DLL;
	VK_CHECK( VK_INTERNAL_ERROR( !( VK_DLL = SysDllLoad( "vulkan-1.dll" ) ) ) );

	vkGetInstanceProcAddr = ( PFN_vkGetInstanceProcAddr ) SysGetProcAddr( VK_DLL, "vkGetInstanceProcAddr" );
	vkCreateInstance = ( PFN_vkCreateInstance ) vkGetInstanceProcAddr( 0, "vkCreateInstance" );
	vkEnumerateInstanceExtensionProperties =
		( PFN_vkEnumerateInstanceExtensionProperties ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceExtensionProperties" );
	vkEnumerateInstanceLayerProperties =
		( PFN_vkEnumerateInstanceLayerProperties ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceLayerProperties" );
	vkEnumerateInstanceVersion = ( PFN_vkEnumerateInstanceVersion ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceVersion" );

	u32 vkExtsNum = 0;
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, 0 ) );
	std::vector<VkExtensionProperties> givenExts( vkExtsNum );
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, std::data( givenExts ) ) );
	for( std::string_view requiredExt : ENABLED_INST_EXTS )
	{
		bool foundExt = false;
		for( VkExtensionProperties& availableExt : givenExts )
		{
			if( requiredExt == availableExt.extensionName )
			{
				foundExt = true;
				break;
			}
		}
		VK_CHECK( VK_INTERNAL_ERROR( !foundExt ) );
	};

	u32 layerCount = 0;
	VK_CHECK( vkEnumerateInstanceLayerProperties( &layerCount, 0 ) );
	std::vector<VkLayerProperties> layersAvailable( layerCount );
	VK_CHECK( vkEnumerateInstanceLayerProperties( &layerCount, std::data( layersAvailable ) ) );
	for( std::string_view requiredLayer : LAYERS )
	{
		bool foundLayer = false;
		for( VkLayerProperties& availableLayer : layersAvailable )
		{
			if( requiredLayer == availableLayer.layerName )
			{
				foundLayer = true;
				break;
			}
		}
		VK_CHECK( VK_INTERNAL_ERROR( !foundLayer ) );
	}


	VkInstance vkInstance = 0;
	VkDebugUtilsMessengerEXT vkDbgUtilsMsgExt = 0;

	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VK_CHECK( vkEnumerateInstanceVersion( &appInfo.apiVersion ) );
	VK_CHECK( VK_INTERNAL_ERROR( appInfo.apiVersion < VK_API_VERSION_1_4 ) );

	VkInstanceCreateInfo instInfo = { 
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = std::size( LAYERS ),
		.ppEnabledLayerNames = LAYERS,
		.enabledExtensionCount = std::size( ENABLED_INST_EXTS ),
		.ppEnabledExtensionNames = ENABLED_INST_EXTS,
	};
#ifdef _VK_DEBUG_

	VkValidationFeaturesEXT vkValidationFeatures = { 
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.enabledValidationFeatureCount = std::size( enabledValidationFeats ),
		.pEnabledValidationFeatures = enabledValidationFeats,
	};

	VkDebugUtilsMessengerCreateInfoEXT vkDbgExt = { 
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.pNext = &vkValidationFeatures,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | 
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = VkDbgUtilsMsgCallback,
	};

	instInfo.pNext = &vkDbgExt;
#endif
	VK_CHECK( vkCreateInstance( &instInfo, 0, &vkInstance ) );

	VkLoadInstanceProcs( vkInstance, *vkGetInstanceProcAddr );

#ifdef _VK_DEBUG_
	VK_CHECK( vkCreateDebugUtilsMessengerEXT( vkInstance, &vkDbgExt, 0, &vkDbgUtilsMsgExt ) );
#endif

	return { .dll = VK_DLL, .hndl = vkInstance, .dbgMsg = vkDbgUtilsMsgExt };
}

#define VMA_IMPLEMENTATION
#include "vk_device.h"

#include "vk_pso.h"

struct virtual_frame
{
	vk_gpu_timer gpuTimer;

	std::shared_ptr<vk_buffer>		pViewData;

	VkCommandPool	cmdPool;
	VkCommandBuffer cmdBuff;
	VkSemaphore		canGetImgSema;

	u16 viewDataIdx;
};

inline virtual_frame VkCreateVirtualFrame( vk_device_ctx& dc )
{
	virtual_frame vrtFrame = {};

	VkCommandPoolCreateInfo cmdPoolInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = dc.gfxQueue.index,
	};
	VK_CHECK( vkCreateCommandPool( dc.device, &cmdPoolInfo, 0, &vrtFrame.cmdPool ) );

	VkCommandBufferAllocateInfo cmdBuffAllocInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vrtFrame.cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VK_CHECK( vkAllocateCommandBuffers( dc.device, &cmdBuffAllocInfo, &vrtFrame.cmdBuff ) );

	VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VK_CHECK( vkCreateSemaphore( dc.device, &semaInfo, 0, &vrtFrame.canGetImgSema ) );

	vrtFrame.gpuTimer = VkMakeGpuTimer( dc.device, 1, dc.timestampPeriod );

	buffer_info queryBuff = {
		.name = "Buff_TimestampQueries",
		.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.elemCount = vrtFrame.gpuTimer.queryCount,
		.stride = sizeof( u64 ),
		.usage = buffer_usage::HOST_VISIBLE
	};
	vrtFrame.gpuTimer.resultBuff = dc.CreateBuffer( queryBuff );

	return vrtFrame;
}

#include "vk_swapchain.h"

#include "r_data_structs.h"

struct vk_rendering_info
{
	VkViewport                                 viewport;
	VkRect2D                                   scissor;
	std::span<const VkRenderingAttachmentInfo> colorAttachments;
	const VkRenderingAttachmentInfo const*     pDepthAttachment;
};

// TODO: add dynamic state params too 
struct vk_scoped_dynamic_renderpass
{
	VkCommandBuffer cmdBuff;

	vk_scoped_dynamic_renderpass( VkCommandBuffer _cmdBuff, const VkRenderingInfo& renderInfo ) 
	{
		this->cmdBuff = _cmdBuff;
		vkCmdBeginRendering( cmdBuff, &renderInfo );
	}
	~vk_scoped_dynamic_renderpass()
	{
		vkCmdEndRendering( cmdBuff );
	}
};

struct vk_command_buffer
{
	VkCommandBuffer hndl;
	VkPipelineLayout bindlessPipelineLayout;
	VkDescriptorSet bindlessDescriptorSet;
	VkPipelineBindPoint currentBindPoint;

	vk_command_buffer( VkCommandBuffer cmdBuff, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet )
	{
		this->hndl = cmdBuff;
		this->bindlessPipelineLayout = pipelineLayout;
		this->bindlessDescriptorSet = descriptorSet;
		this->currentBindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;

		VkCommandBufferBeginInfo cmdBufBegInfo = { 
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		vkBeginCommandBuffer( hndl, &cmdBufBegInfo );
	}

	vk_scoped_label CmdIssueScopedLabel( const char* labelName, DXPacked::XMCOLOR col = {} )
	{
		return { hndl, labelName, col };
	}

	vk_scoped_dynamic_renderpass CmdIssueDynamicScopedRenderPass(
		const VkRenderingAttachmentInfo* pColInfos,
		u32 colAttachmentCount,
		const VkRenderingAttachmentInfo* pDepthInfo,
		const VkRect2D& renderArea,
		u32 layerCount = 1
	) {
		VkRenderingInfo renderInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = renderArea,
			.layerCount = layerCount,
			.colorAttachmentCount = colAttachmentCount,
			.pColorAttachments = pColInfos,
			.pDepthAttachment = pDepthInfo
		};
		return { hndl, renderInfo };
	}

	void CmdBindPipelineAndBindlessDesc( VkPipeline pipeline, const VkPipelineBindPoint bindPoint )
	{
		if( bindPoint != currentBindPoint )
		{
			vkCmdBindDescriptorSets( hndl, bindPoint, bindlessPipelineLayout,0, 1, &bindlessDescriptorSet, 0, 0 );
			currentBindPoint = bindPoint;
		}
		vkCmdBindPipeline( hndl, bindPoint, pipeline );
	}

	void CmdPushConstants( const void* pData, u32 size )
	{
		VkPushConstantsInfo pushConstInfo = {
			.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
			.layout = bindlessPipelineLayout,
			.stageFlags = VK_SHADER_STAGE_ALL,
			.offset = 0,
			.size = size,
			.pValues = pData
		};
		vkCmdPushConstants2( hndl, &pushConstInfo );
	}

	void CmdDispatch( DirectX::XMUINT3 numWorkgroups )
	{
		vkCmdDispatch( hndl, numWorkgroups.x, numWorkgroups.y, numWorkgroups.z );
	}

	void CmdDrawIndexedIndirectCount( 
		const vk_buffer& idxBuffer, 
		VkIndexType      idxType,
		const vk_buffer& drawCmds, 
		const vk_buffer& drawCount,
		u32              maxDrawCount
	) {
		vkCmdBindIndexBuffer( hndl, idxBuffer.hndl, 0, idxType );
		vkCmdDrawIndexedIndirectCount( hndl, drawCmds.hndl, 0, drawCount.hndl, 0, maxDrawCount, sizeof( draw_command ) );
	}

	void CmdPipelineBarriers( 
		std::span<VkBufferMemoryBarrier2> buffBarriers, 
		std::span<VkImageMemoryBarrier2> imgBarriers 
	) {
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = ( u32 ) std::size( buffBarriers ),
			.pBufferMemoryBarriers = std::data( buffBarriers ),
			.imageMemoryBarrierCount = ( u32 ) std::size( imgBarriers ),
			.pImageMemoryBarriers = std::data( imgBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdCopyBuffer( const vk_buffer& src, const vk_buffer& dst, const VkBufferCopy& copyRegion )
	{
		vkCmdCopyBuffer( hndl, src.hndl, dst.hndl, 1, &copyRegion );
	}

	void CmdCopyBufferToSingleImageSubresource( const vk_buffer& src, u64 srcOffset, const vk_image& dst )
	{
		VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( dst.format );
		VkBufferImageCopy imgCopyRegion = {
			.bufferOffset = srcOffset,
			.imageSubresource = {
				.aspectMask = aspectFlags,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1
	        },
			.imageOffset = {},
			.imageExtent = { u32( dst.width ), u32( dst.height ), 1 }
		};

		vkCmdCopyBufferToImage( hndl, src.hndl, dst.hndl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );
	}

	void CmdPipelineMemoryBarriers( std::span<VkMemoryBarrier2> memBarriers ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.memoryBarrierCount = ( u32 ) std::size( memBarriers ),
			.pMemoryBarriers = std::data( memBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdPipelineBufferBarriers( std::span<VkBufferMemoryBarrier2> buffBarriers ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = ( u32 ) std::size( buffBarriers ),
			.pBufferMemoryBarriers = std::data( buffBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdPipelineImageBarriers( std::span<VkImageMemoryBarrier2> imgBarriers ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = ( u32 ) std::size( imgBarriers ),
			.pImageMemoryBarriers = std::data( imgBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdFillVkBuffer( const vk_buffer& vkBuffer, u32 fillValue )
	{
		vkCmdFillBuffer( hndl, vkBuffer.hndl, 0, vkBuffer.sizeInBytes, fillValue );
	}

	void CmdEndCmbBuffer()
	{
		VK_CHECK( vkEndCommandBuffer( hndl ) );
	}

	//~vk_command_buffer()
	//{
	//	VK_CHECK( vkEndCommandBuffer( cmdBuff ) );
	//}
};


// TODO: should make obsolete
struct vk_program
{
	VkPipelineLayout			pipeLayout;
	VkDescriptorUpdateTemplate	descUpdateTemplate;
	VkShaderStageFlags			pushConstStages;
	VkDescriptorSetLayout       descSetLayout;
	group_size					groupSize;
};


inline static void VkReflectShaderLayout(
	const VkPhysicalDeviceProperties&			gpuProps,
	const std::vector<u8>&						spvByteCode,
	std::vector<VkDescriptorSetLayoutBinding>&	descSetBindings,
	std::vector<VkPushConstantRange>&			pushConstRanges,
	group_size&									gs,
	char*										entryPointName,
	u64											entryPointNameStrLen 
){
	SpvReflectShaderModule shaderReflection;
	VK_CHECK( (VkResult) spvReflectCreateShaderModule( std::size( spvByteCode ) * sizeof( spvByteCode[ 0 ] ),
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

inline static vk_program VkMakePipelineProgram(
	VkDevice							vkDevice,
	const VkPhysicalDeviceProperties&	gpuProps,
	VkPipelineBindPoint					bindPoint,
	vk_shader_list						shaders,
	VkDescriptorSetLayout				bindlessLayout 
){
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


#include "AssetCompiler/asset_compiler.h"


inline VkFormat VkGetFormat( texture_format t )
{
	switch( t )
	{
	case TEXTURE_FORMAT_RBGA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
	case TEXTURE_FORMAT_RBGA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
	case TEXTURE_FORMAT_BC1_RGB_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
	case TEXTURE_FORMAT_BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
	case TEXTURE_FORMAT_UNDEFINED: assert( 0 );
	}
}
inline VkImageType VkGetImageType( texture_type t )
{
	switch( t )
	{
	case TEXTURE_TYPE_1D: return VK_IMAGE_TYPE_1D;
	case TEXTURE_TYPE_2D: return VK_IMAGE_TYPE_2D;
	case TEXTURE_TYPE_3D: return VK_IMAGE_TYPE_3D;
	default: assert( 0 ); return VK_IMAGE_TYPE_MAX_ENUM;
	}
}
// TODO: default types ?
inline VkFilter VkGetFilterTypeFromGltf( gltf_sampler_filter f )
{
	switch( f )
	{
	case GLTF_SAMPLER_FILTER_NEAREST:
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR:
	return VK_FILTER_NEAREST;

	case GLTF_SAMPLER_FILTER_LINEAR:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR:
	default:
	return VK_FILTER_LINEAR;
	}
}
inline VkSamplerMipmapMode VkGetMipmapTypeFromGltf( gltf_sampler_filter m )
{
	switch( m )
	{
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST:
	return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR:
	default:
	return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}
inline VkSamplerAddressMode VkGetAddressModeFromGltf( gltf_sampler_address_mode a )
{
	switch( a )
	{
	case GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case GLTF_SAMPLER_ADDRESS_MODE_REPEAT: default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}
// TODO: ensure mipmapMode in assetcmpl
// TODO: addrModeW ?
// TODO: more stuff ?
inline VkSamplerCreateInfo VkMakeSamplerInfo( sampler_config config )
{
	assert( 0 );
	VkSamplerCreateInfo vkSamplerInfo = { 
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VkGetFilterTypeFromGltf( config.mag ),
		.minFilter = VkGetFilterTypeFromGltf( config.min ),
		.mipmapMode =  VkGetMipmapTypeFromGltf( config.min ),
		.addressModeU = VkGetAddressModeFromGltf( config.addrU ),
		.addressModeV = VkGetAddressModeFromGltf( config.addrV )
	};

	return vkSamplerInfo;
}

inline image_info GetImageInfoFromMetadata( const image_metadata& meta, VkImageUsageFlags usageFlags )
{
	return { 
		.format =  VkGetFormat( meta.format ),
		.usg = usageFlags,
		.width = meta.width,
		.height = meta.height,
		.layerCount = meta.layerCount,
		.mipCount =  meta.mipCount,
	};
}

#define HTVK_NO_SAMPLER_REDUCTION VK_SAMPLER_REDUCTION_MODE_MAX_ENUM

// TODO: AddrMode ?
inline VkSampler VkMakeSampler(
	VkDevice				vkDevice,
	VkSamplerReductionMode	reductionMode = HTVK_NO_SAMPLER_REDUCTION,
	VkFilter				filter = VK_FILTER_LINEAR,
	VkSamplerAddressMode	addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	VkSamplerMipmapMode		mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
) {
	VkSamplerReductionModeCreateInfo reduxInfo = { 
		.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
		.reductionMode = reductionMode,
	};

	VkSamplerCreateInfo samplerInfo = { 
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = ( reductionMode == VK_SAMPLER_REDUCTION_MODE_MAX_ENUM ) ? 0 : &reduxInfo,
		.magFilter = filter,
		.minFilter = filter,
		.mipmapMode = mipmapMode,
		.addressModeU = addressMode,
		.addressModeV = addressMode,
		.addressModeW = addressMode,
		.maxAnisotropy = 1.0f,
		.minLod = 0,
		.maxLod = VK_LOD_CLAMP_NONE,
		.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
	};
	
	VkSampler sampler;
	VK_CHECK( vkCreateSampler( vkDevice, &samplerInfo, 0, &sampler ) );
	return sampler;
}


#include "ht_geometry.h"


#include "imgui/imgui.h"

inline u32 GroupCount( u32 invocationCount, u32 workGroupSize )
{
	return ( invocationCount + workGroupSize - 1 ) / workGroupSize;
}

inline void VkCmdBeginRendering(
	VkCommandBuffer		cmdBuff,
	const VkRenderingAttachmentInfo* pColInfos,
	u32 colAttachmentCount,
	const VkRenderingAttachmentInfo* pDepthInfo,
	const VkRect2D& scissor,
	u32 layerCount = 1
) {
	VkRenderingInfo renderInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = scissor,
		.layerCount = layerCount,
		.colorAttachmentCount = colAttachmentCount,
		.pColorAttachments = pColInfos,
		.pDepthAttachment = pDepthInfo
	};
	vkCmdBeginRendering( cmdBuff, &renderInfo );
}

// TODO: better double buffer vert + idx
// TODO: move spv shaders into exe folder
struct imgui_context
{
	vk_buffer                   vtxBuffs[ VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	vk_buffer                   idxBuffs[ VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	std::shared_ptr<vk_image>   pFontsImg;
	VkImageView                 fontsView;
	VkSampler                   fontSampler;

	VkDescriptorSetLayout       descSetLayout;
	VkPipelineLayout            pipelineLayout;
	VkDescriptorUpdateTemplate  descTemplate;
	VkPipeline	                pipeline;
	
	void Init( vk_device_ctx& dc )
	{
		fontSampler = VkMakeSampler( 
			dc.device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );

		VkDescriptorSetLayoutBinding descSetBindings[ 2 ] = {};
		descSetBindings[ 0 ].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descSetBindings[ 0 ].descriptorCount = 1;
		descSetBindings[ 0 ].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		descSetBindings[ 1 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descSetBindings[ 1 ].descriptorCount = 1;
		descSetBindings[ 1 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		descSetBindings[ 1 ].pImmutableSamplers = &fontSampler;
		descSetBindings[ 1 ].binding = 1;

		VkDescriptorSetLayoutCreateInfo descSetInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		descSetInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
		descSetInfo.bindingCount = std::size( descSetBindings );
		descSetInfo.pBindings = descSetBindings;
		VkDescriptorSetLayout descSetLayout = {};
		VK_CHECK( vkCreateDescriptorSetLayout( dc.device, &descSetInfo, 0, &descSetLayout ) );

		VkPushConstantRange pushConst = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( float ) * 4 };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConst;

		VkPipelineLayout pipelineLayout = {};
		VK_CHECK( vkCreatePipelineLayout( dc.device, &pipelineLayoutInfo, 0, &pipelineLayout ) );

		VkDescriptorUpdateTemplateEntry entries[ 2 ] = {};
		entries[ 0 ].descriptorCount = 1;
		entries[ 0 ].descriptorType = descSetBindings[ 0 ].descriptorType;
		entries[ 0 ].offset = 0;
		entries[ 0 ].stride = sizeof( vk_descriptor_info );
		entries[ 1 ].descriptorCount = 1;
		entries[ 1 ].descriptorType = descSetBindings[ 1 ].descriptorType;
		entries[ 1 ].offset = sizeof( vk_descriptor_info );
		entries[ 1 ].stride = sizeof( vk_descriptor_info );
		entries[ 1 ].dstBinding = descSetBindings[ 1 ].binding;

		VkDescriptorUpdateTemplateCreateInfo templateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO };
		templateInfo.descriptorUpdateEntryCount = std::size( entries );
		templateInfo.pDescriptorUpdateEntries = std::data( entries );
		templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
		templateInfo.descriptorSetLayout = descSetLayout;
		templateInfo.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		templateInfo.pipelineLayout = pipelineLayout;

		VkDescriptorUpdateTemplate descTemplate = {};
		VK_CHECK( vkCreateDescriptorUpdateTemplate( dc.device, &templateInfo, 0, &descTemplate ) );

		vk_shader2 vert = VkLoadShader2( "bin/SpirV/vertex_ImGuiVsMain.spirv", dc.device );
		vk_shader2 frag = VkLoadShader2( "bin/SpirV/pixel_ImGuiPsMain.spirv", dc.device );

		vk_gfx_pipeline_state guiState = {};
		guiState.blendCol = VK_TRUE; 
		guiState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		guiState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		guiState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		guiState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		guiState.depthWrite = VK_FALSE;
		guiState.depthTestEnable = VK_FALSE;
		guiState.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		guiState.polyMode = VK_POLYGON_MODE_FILL;
		guiState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		guiState.cullFlags = VK_CULL_MODE_NONE;

		VkPipelineShaderStageCreateInfo shaderStages[] = { VkMakePipelineShaderInfo( vert ), VkMakePipelineShaderInfo( frag ) };
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		VkPipeline pipeline = VkMakeGfxPipeline(
			dc.device, shaderStages, dynamicStates, 
			&renderCfg.desiredSwapchainFormat, 1, VK_FORMAT_UNDEFINED, pipelineLayout, guiState );
	}

	void InitResources( vk_device_ctx& dc, VkFormat colDstFormat )
	{
		vtxBuffs[ 0 ] = dc.CreateBuffer( { 
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
			.elemCount = 16 * KB, 
			.stride = 1, 
			.usage = buffer_usage::HOST_VISIBLE } );

		idxBuffs[ 0 ] = dc.CreateBuffer( { 
			.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
			.elemCount = 16 * KB, 
			.stride = 1, 
			.usage = buffer_usage::HOST_VISIBLE } );
		vtxBuffs[ 1 ] = dc.CreateBuffer( { 
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
			.elemCount = 16 * KB, 
			.stride = 1, 
			.usage = buffer_usage::HOST_VISIBLE } );
		idxBuffs[ 1 ] = dc.CreateBuffer( { 
			.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
			.elemCount = 16 * KB, 
			.stride = 1, 
			.usage = buffer_usage::HOST_VISIBLE } );

		u8* pixels = 0;
		u32 width = 0, height = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32( &pixels, ( int* ) &width, ( int* ) &height );

		constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
		pFontsImg = std::make_shared<vk_image>( dc.CreateImage( {
			.name = "Img_ImGuiFonts",
			.format = colDstFormat,
			.usg = usgFlags,
			.width = ( u16 ) width,
			.height = ( u16 ) height,
			.layerCount = 1,
			.mipCount = 1,
		} ) );

		VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( pFontsImg->format );
		VkHostImageLayoutTransitionInfo hostImgLayoutTransitionInfo = {
			.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO,
			.image = pFontsImg->hndl,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.subresourceRange = {
				.aspectMask = aspectFlags,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
		    },
		};
	
		dc.TransitionImageLayout( &hostImgLayoutTransitionInfo, 1 );
		dc.CopyMemoryToImage( *pFontsImg, pixels );
	}

	// TODO: overdraw more efficiently 
	void DrawUiPass(
		VkCommandBuffer cmdBuff,
		const VkRenderingAttachmentInfo* pColInfo,
		const VkRenderingAttachmentInfo* pDepthInfo,
		const VkRect2D& scissor,
		u64 frameIdx
	) {
		static_assert( sizeof( ImDrawVert ) == sizeof( imgui_vertex ) );
		static_assert( sizeof( ImDrawIdx ) == 2 );

		using namespace DirectX;

		const ImDrawData* guiDrawData = ImGui::GetDrawData();

		const vk_buffer& vtxBuff = vtxBuffs[ frameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
		const vk_buffer& idxBuff = idxBuffs[ frameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];

		assert( guiDrawData->TotalVtxCount < u16( -1 ) );
		assert( guiDrawData->TotalVtxCount * sizeof( ImDrawVert ) < vtxBuff.sizeInBytes );

		ImDrawVert* vtxDst = ( ImDrawVert* ) vtxBuff.hostVisible;
		ImDrawIdx* idxDst = ( ImDrawIdx* ) idxBuff.hostVisible;
		for( u64 ci = 0; ci < guiDrawData->CmdListsCount; ++ci )
		{
			const ImDrawList* cmdList = guiDrawData->CmdLists[ ci ];
			std::memcpy( vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof( ImDrawVert ) );
			std::memcpy( idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof( ImDrawIdx ) );
			vtxDst += cmdList->VtxBuffer.Size;
			idxDst += cmdList->IdxBuffer.Size;
		}

		float scale[ 2 ] = { 2.0f / guiDrawData->DisplaySize.x, 2.0f / guiDrawData->DisplaySize.y };
		float move[ 2 ] = { -1.0f - guiDrawData->DisplayPos.x * scale[ 0 ], -1.0f - guiDrawData->DisplayPos.y * scale[ 1 ] };
		XMFLOAT4 pushConst = { scale[ 0 ],scale[ 1 ],move[ 0 ],move[ 1 ] };


		vk_scoped_label label = { cmdBuff,"Draw Imgui Pass",{} };

		VkCmdBeginRendering( cmdBuff, pColInfo, pColInfo ? 1 : 0, pDepthInfo, scissor, 1 );
		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );

		VkDescriptorImageInfo descImgInfo = { fontSampler, fontsView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
		vk_descriptor_info pushDescs[] = { Descriptor( vtxBuff ), descImgInfo };

		vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, descTemplate, pipelineLayout, 0, pushDescs );
		vkCmdPushConstants( cmdBuff, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( pushConst ), &pushConst );
		vkCmdBindIndexBuffer( cmdBuff, idxBuff.hndl, 0, VK_INDEX_TYPE_UINT16 );


		// (0,0) unless using multi-viewports
		XMFLOAT2 clipOff = { guiDrawData->DisplayPos.x, guiDrawData->DisplayPos.y };
		// (1,1) unless using retina display which are often (2,2)
		XMFLOAT2 clipScale = { guiDrawData->FramebufferScale.x, guiDrawData->FramebufferScale.y };

		u32 vtxOffset = 0;
		u32 idxOffset = 0;
		for( u64 li = 0; li < guiDrawData->CmdListsCount; ++li )
		{
			const ImDrawList* cmdList = guiDrawData->CmdLists[ li ];
			for( u64 ci = 0; ci < cmdList->CmdBuffer.Size; ++ci )
			{
				const ImDrawCmd* pCmd = &cmdList->CmdBuffer[ ci ];
				// Project scissor/clipping rectangles into framebuffer space
				XMFLOAT2 clipMin = { ( pCmd->ClipRect.x - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.y - clipOff.y ) * clipScale.y };
				XMFLOAT2 clipMax = { ( pCmd->ClipRect.z - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.w - clipOff.y ) * clipScale.y };

				// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
				clipMin = { std::max( clipMin.x, 0.0f ), std::max( clipMin.y, 0.0f ) };
				clipMax = { std::min( clipMax.x, ( float ) scissor.extent.width ), 
					std::min( clipMax.y, ( float ) scissor.extent.height ) };

				if( clipMax.x < clipMin.x || clipMax.y < clipMin.y ) continue;

				VkRect2D scissor = { i32( clipMin.x ), i32( clipMin.y ), u32( clipMax.x - clipMin.x ), u32( clipMax.y - clipMin.y ) };
				vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

				vkCmdDrawIndexed( cmdBuff, pCmd->ElemCount, 1, pCmd->IdxOffset + idxOffset, pCmd->VtxOffset + vtxOffset, 0 );
			}
			idxOffset += cmdList->IdxBuffer.Size;
			vtxOffset += cmdList->VtxBuffer.Size;
		}

		vkCmdEndRendering( cmdBuff );
	}

};

enum class debug_draw_type : u8
{
	LINE,
	TRIANGLE,
};
// TODO: use instancing for drawing ?
// TODO: double buffer debug geometry ?
struct debug_context
{
	std::shared_ptr<vk_buffer> pLinesBuff;
	std::shared_ptr<vk_buffer> pTrisBuff;
	std::shared_ptr<vk_buffer> pDrawCount;

	vk_program	pipeProg;
	VkPipeline	drawAsLines;
	VkPipeline	drawAsTriangles;

	void Init( vk_device_ctx& dc, VkPipelineLayout globalLayout, renderer_config& rndCfg )
	{
		vk_shader vert = VkLoadShader( "Shaders/v_cpu_dbg_draw.vert.spv", dc.device );
		vk_shader frag = VkLoadShader( "Shaders/f_pass_col.frag.spv", dc.device );

		static_assert( worldLeftHanded );
		vk_gfx_pipeline_state lineDrawPipelineState = {
			.polyMode = VK_POLYGON_MODE_LINE,
			.cullFlags = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
			.depthWrite = VK_FALSE,
			.depthTestEnable = VK_FALSE,
			.blendCol = VK_FALSE,
		};
		drawAsLines = VkMakeGfxPipeline( dc.device, 0, globalLayout, vert.module, frag.module, 
			&rndCfg.desiredColorFormat, 1, VK_FORMAT_UNDEFINED, lineDrawPipelineState );

		vk_gfx_pipeline_state triDrawPipelineState = {
			.polyMode = VK_POLYGON_MODE_FILL,
			.cullFlags = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.depthWrite = VK_TRUE,
			.depthTestEnable = VK_TRUE,
			.blendCol = VK_TRUE
		};
		drawAsTriangles = VkMakeGfxPipeline( dc.device, 0, globalLayout, vert.module, frag.module, 
			&rndCfg.desiredColorFormat, 1, rndCfg.desiredDepthFormat, triDrawPipelineState );

		vkDestroyShaderModule( dc.device, vert.module, 0 );
		vkDestroyShaderModule( dc.device, frag.module, 0 );

		pDrawCount = std::make_shared<vk_buffer>( dc.CreateBuffer( {
		.name = "Buff_DbgDrawCount",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } ) );  
	}

	void InitData( vk_device_ctx& dc, u32 dbgLinesSizeInBytes, u32 dbgTrianglesSizeInBytes )
	{
		pLinesBuff = std::make_shared<vk_buffer>( dc.CreateBuffer( { 
			.name = "Buff_DbgLines",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
			.elemCount = dbgLinesSizeInBytes, 
			.stride = 1, 
			.usage = buffer_usage::HOST_VISIBLE } ) );

		pTrisBuff = std::make_shared<vk_buffer>( dc.CreateBuffer( { 
			.name = "Buff_DbgTris",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
			.elemCount = dbgTrianglesSizeInBytes, 
			.stride = 1, 
			.usage = buffer_usage::HOST_VISIBLE } ) );
	}

	// NOTE: must be aligned properly to work
	void UploadDebugGeometry()
	{
		auto unitCube = GenerateBoxWithBounds( BOX_MIN, BOX_MAX );
		auto lineVtxBuff = BoxVerticesAsLines( unitCube );
		auto trisVtxBuff = BoxVerticesAsTriangles( unitCube );

		assert( pLinesBuff->sizeInBytes >= BYTE_COUNT( lineVtxBuff ) );
		assert( pTrisBuff->sizeInBytes >= BYTE_COUNT( trisVtxBuff ) );
		std::memcpy( pLinesBuff->hostVisible, std::data( lineVtxBuff ), BYTE_COUNT( lineVtxBuff ) );
		std::memcpy( pTrisBuff->hostVisible, std::data( trisVtxBuff ), BYTE_COUNT( trisVtxBuff ) );
	}

	// TODO: multi draw with params
	void DrawCPU( 
		vk_command_buffer&        cmdBuff, 
		const vk_rendering_info&  renderingInfo, 
		const char*               name,
		debug_draw_type           ddType, 
		u64                       viewAddr, 
		u32                       viewIdx,
		const mat4&      transf,
		u32 color
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( name, {} );
		auto dynamicRendering = cmdBuff.CmdIssueDynamicScopedRenderPass( std::data( renderingInfo.colorAttachments ), 
			std::size( renderingInfo.colorAttachments ), renderingInfo.pDepthAttachment, renderingInfo.scissor, 1 );
		
		vkCmdSetScissor( cmdBuff.hndl, 0, 1, &renderingInfo.scissor );
		vkCmdSetViewport( cmdBuff.hndl, 0, 1, &renderingInfo.viewport );
		
		VkPipeline vkPipeline = ( ddType == debug_draw_type::TRIANGLE ) ? drawAsTriangles : drawAsLines;

		cmdBuff.CmdBindPipelineAndBindlessDesc( vkPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS );

		u64 vtxAddr = ( ddType == debug_draw_type::TRIANGLE ) ? pTrisBuff->devicePointer : pLinesBuff->devicePointer;
		u32 vertexCount = ( ddType == debug_draw_type::TRIANGLE ) ? std::size( boxTrisIndices ) : std::size( boxLineIndices );
#pragma pack(push, 1)
		struct debug_cpu_push
		{
			mat4 transf;
			uint64_t vtxAddr;
			uint64_t viewAddr;
			uint viewIdx;
			uint color;
		} pc = { transf, vtxAddr, viewAddr, viewIdx, color };
#pragma pack(pop)
		cmdBuff.CmdPushConstants( &pc, sizeof( pc ) );
		vkCmdDraw( cmdBuff.hndl, vertexCount, 1, 0, 0 );
	}
};


static entities_data entities;


static vk_buffer globVertexBuff;
static vk_buffer indexBuff;
static vk_buffer meshBuff;

static vk_buffer meshletBuff;
static vk_buffer meshletDataBuff;

// TODO:
static vk_buffer transformsBuff;

static vk_buffer materialsBuff;
static vk_buffer instDescBuff;
static vk_buffer lightsBuff;


static vk_buffer intermediateIndexBuff;
static vk_buffer indirectMergedIndexBuff;

static vk_buffer drawCmdAabbsBuff;
static vk_buffer drawCmdDbgBuff;

constexpr char glbPath[] = "D:\\3d models\\cyberbaron\\cyberbaron.glb";
constexpr char drakPath[] = "Assets/cyberbaron.drak";

// TODO: recycle_queue for more objects
struct staging_manager
{
	struct upload_job
	{
		std::shared_ptr<vk_buffer>	buff;
		u64			                frameId;
	};
	std::vector<upload_job>		pendingUploads;
	u64							semaSignalCounter;

	inline void PushForRecycle( std::shared_ptr<vk_buffer>& stagingBuff, u64 currentFrameId )
	{
		pendingUploads.push_back( { stagingBuff,currentFrameId } );
	}

	template<typename T>
	inline std::shared_ptr<vk_buffer> GetStagingBufferAndCopyHostData( 
		vk_device_ctx& dc, 
		std::span<const T> dataIn,
		u64 currentFrameId
	) {
		u64 sizeInBytes = std::size( dataIn ) * sizeof( T );
		auto stagingBuff = std::make_shared<vk_buffer>( dc.CreateBuffer( { 
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .elemCount = size, .stride = 1, .usage = buffer_usage::STAGING } ) );
		std::memcpy( stagingBuff->hostVisible, std::data( dataIn ), sizeInBytes );

		PushForRecycle( stagingBuff, currentFrameID );

		return stagingBuff;
	}
};




inline static void
StagingManagerUploadBuffer( 
	vk_device_ctx& dc,
	staging_manager& stgMngr, 
	vk_command_buffer& cmdBuff, 
	std::span<const u8> dataIn,
	const vk_buffer& dst,
	u64 currentFrameId 
) {
	
	//tagingManagerPushForRecycle( stgMngr, stagingBuff, currentFrameId );

	
}

static staging_manager stagingManager;

static vk_program   dbgDrawProgram = {};
static VkPipeline   gfxDrawIndirDbg = {};

struct culling_ctx
{
	std::shared_ptr<vk_buffer> pInstanceOccludedCache;
	std::shared_ptr<vk_buffer> pClusterOccludedCache;
	std::shared_ptr<vk_buffer> pCompactedDrawArgs;
	std::shared_ptr<vk_buffer> pDrawCmds;
	std::shared_ptr<vk_buffer> pDrawCount;
	std::shared_ptr<vk_buffer> pAtomicWgCounter;
	std::shared_ptr<vk_buffer> pDispatchIndirect;

	VkPipeline		compPipeline;
	VkPipeline      compExpanderPipe;
	VkPipeline      compClusterCullPipe;

	void Init( vk_device_ctx& dc, VkPipelineLayout pipelineLayout )
	{
		vk_shader drawCull = VkLoadShader( "Shaders/c_draw_cull.comp.spv", dc.device );
		compPipeline = VkMakeComputePipeline( 
			dc.device, 0, pipelineLayout, drawCull.module, 
			{ dc.waveSize }, dc.waveSize, SHADER_ENTRY_POINT, "Pipeline_Comp_DrawCull" );

		vk_shader clusterCull = VkLoadShader( "Shaders/c_meshlet_cull.comp.spv", dc.device );
		compClusterCullPipe = VkMakeComputePipeline( 
			dc.device, 0, pipelineLayout, clusterCull.module, {}, dc.waveSize, SHADER_ENTRY_POINT, "Pipeline_Comp_ClusterCull" );

		vk_shader expansionComp = VkLoadShader( "Shaders/c_id_expander.comp.spv", dc.device );
		compExpanderPipe = VkMakeComputePipeline( 
			dc.device, 0, pipelineLayout, expansionComp.module, {}, dc.waveSize, SHADER_ENTRY_POINT, "Pipeline_Comp_iD_Expander" );

		vkDestroyShaderModule( dc.device, drawCull.module, 0 );
		vkDestroyShaderModule( dc.device, expansionComp.module, 0 );
		vkDestroyShaderModule( dc.device, clusterCull.module, 0 );

		pDrawCount = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_DrawCount",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | 
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.elemCount = 1,
			.stride = sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } ) );

		pAtomicWgCounter = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_AtomicWgCounter",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.elemCount = 1,
			.stride = sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } ) ); 

		pDispatchIndirect = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_DispatchIndirect",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.elemCount = 1,
			.stride = sizeof( dispatch_command ),
			.usage = buffer_usage::GPU_ONLY } ) );  
	}

	void InitSceneDependentData( vk_device_ctx& dc, u32 instancesUpperBound, u32 meshletUpperBound )
	{
		constexpr VkBufferUsageFlags usg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		pInstanceOccludedCache = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_InstanceVisibilityCache",
			.usageFlags = usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = instancesUpperBound,
			.stride = sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } ) );

		pClusterOccludedCache = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_ClusterVisibilityCache",
			.usageFlags = usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = meshletUpperBound,
			.stride = sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } ) );

		pCompactedDrawArgs = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_CompactedDrawArgs",
			.usageFlags = usg,
			.elemCount = meshletUpperBound,
			.stride = sizeof( compacted_draw_args ),
			.usage = buffer_usage::GPU_ONLY } ) );
		pDrawCmds = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_DrawCmds",
			.usageFlags = usg | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT ,
			.elemCount = meshletUpperBound,
			.stride = sizeof( draw_command ),
			.usage = buffer_usage::GPU_ONLY } ) );
	}

	void Execute(
		vk_command_buffer&  cmdBuff, 
		const vk_image&			depthPyramid,
		u32 instCount,
		u16 _camIdx,
		u16 _hizBuffIdx,
		u16 samplerIdx,
		bool latePass
	) {
		// NOTE: wtf Vulkan ?
		constexpr u64 VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Cull Pass",{} );

		if( !latePass )
		{
			cmdBuff.CmdFillVkBuffer( *pInstanceOccludedCache, 0u );
		}
		cmdBuff.CmdFillVkBuffer( *pDrawCount, 0u );
		cmdBuff.CmdFillVkBuffer( *pAtomicWgCounter, 0u );

		std::vector<VkBufferMemoryBarrier2> beginCullBarriers = {
			VkMakeBufferBarrier2( 
				pDrawCmds->hndl,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
				VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
			VkMakeBufferBarrier2( 
				pDrawCount->hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
			VkMakeBufferBarrier2( 
				pDispatchIndirect->hndl,
				0,
				0,
				VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
			VkMakeBufferBarrier2( 
				pAtomicWgCounter->hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		};

		if( !latePass )
		{
			beginCullBarriers.emplace_back( 
				VkMakeBufferBarrier2( pInstanceOccludedCache->hndl,
									  VK_ACCESS_2_TRANSFER_WRITE_BIT,
									  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
									  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
									  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) );
		}

		VkImageMemoryBarrier2 hiZReadBarrier[] = { VkMakeImageBarrier2(
			depthPyramid.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT ) };

		cmdBuff.CmdPipelineBarriers( beginCullBarriers, hiZReadBarrier );

		VkMemoryBarrier2 computeToComputeExecDependency[] = { 
			VkMemoryBarrier2{
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT,
		},
		};

		{
			struct culling_push{ 
				u64 instDescAddr = instDescBuff.devicePointer;
				u64 meshDescAddr = meshBuff.devicePointer;
				u64 visInstsAddr = intermediateIndexBuff.devicePointer;
				u64 drawCmdsAddr;
				u64 compactedArgsAddr;
				u64 instOccCacheAddr;
				u64 atomicWorkgrCounterAddr;
				u64 visInstaceCounterAddr;
				u64 dispatchCmdAddr;
				u32	hizBuffIdx;
				u32	hizSamplerIdx;
				u32 instanceCount;
				u32 camIdx;
				u32 latePass;
			} pushConst = {
					.drawCmdsAddr = pDrawCmds->devicePointer,
					.compactedArgsAddr = pCompactedDrawArgs->devicePointer,
					.instOccCacheAddr = pInstanceOccludedCache->devicePointer,
					.atomicWorkgrCounterAddr = pAtomicWgCounter->devicePointer,
					.visInstaceCounterAddr = pDrawCount->devicePointer,
					.dispatchCmdAddr = pDispatchIndirect->devicePointer,
					.hizBuffIdx = _hizBuffIdx,
					.hizSamplerIdx = samplerIdx,
					.instanceCount = instCount,
					.camIdx = _camIdx,
					.latePass = (u32)latePass
			};

			cmdBuff.CmdBindPipelineAndBindlessDesc( compPipeline, VK_PIPELINE_BIND_POINT_COMPUTE );
			cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
			vkCmdDispatch( cmdBuff.hndl, GroupCount( instCount, 32 ), 1, 1 );
		}
#if  0
		if(0){
			VkBufferMemoryBarrier2 readCmdIndirect[] = {
				VkMakeBufferBarrier2( 
					pDispatchIndirect->hndl,
					VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH )
			};
			cmdBuff.CmdPipelineBufferBarriers( readCmdIndirect );
			cmdBuff.CmdPipelineMemoryBarriers( computeToComputeExecDependency );

			struct
			{
				u64 visInstAddr = intermediateIndexBuff.devicePointer;
				u64 visInstCountAddr = drawMergedCountBuff.devicePointer;
				u64 expandeeAddr = indirectMergedIndexBuff.devicePointer;
				u64 expandeeCountAddr = meshletCountBuff.devicePointer;
				u64 atomicWorkgrCounterAddr = depthAtomicCounterBuff.devicePointer;
				u64 dispatchCmdAddr = dispatchCmdBuff1.devicePointer;
			} pushConst = {};

			vkCmdBindPipeline( cmdBuff.hndl, VK_PIPELINE_BIND_POINT_COMPUTE, compExpanderPipe );
			cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
			vkCmdDispatchIndirect( cmdBuff.hndl, pDispatchIndirect->hndl, 0 );
		}

		if(0){
			VkBufferMemoryBarrier2 readCmdIndirect[] = {
				VkMakeBufferBarrier2( dispatchCmdBuff1.hndl,
									  VK_ACCESS_2_SHADER_WRITE_BIT,
									  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
									  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
									  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH )
			};
			cmdBuff.CmdPipelineBufferBarriers( readCmdIndirect );
			cmdBuff.CmdPipelineMemoryBarriers( computeToComputeExecDependency );

			struct { 
				u64 instDescAddr = instDescBuff.devicePointer;
				u64 meshletDescAddr = meshletBuff.devicePointer;
				u64	inMeshletsIdAddr = indirectMergedIndexBuff.devicePointer;
				u64	inMeshletsCountAddr = meshletCountBuff.devicePointer;
				u64	compactedDrawAddr;
				u64	drawCmdsAddr;
				u64	drawCountAddr;
				u64	dbgDrawCmdsAddr = drawCmdAabbsBuff.devicePointer;
				u32 hizBuffIdx;
				u32	hizSamplerIdx;
				u32 camIdx;
			} pushConst = {
					.compactedDrawAddr = pCompactedDrawArgs->devicePointer,
					.drawCmdsAddr = pDrawCmds->devicePointer,
					.drawCountAddr = pDrawCount->devicePointer,
					.hizBuffIdx = _hizBuffIdx,
					.hizSamplerIdx = samplerIdx,
					.camIdx = _camIdx
			};

			vkCmdBindPipeline( cmdBuff.hndl, VK_PIPELINE_BIND_POINT_COMPUTE, compClusterCullPipe );
			cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
			vkCmdDispatchIndirect( cmdBuff.hndl, dispatchCmdBuff1.hndl, 0 );
		}
#endif
		VkBufferMemoryBarrier2 endCullBarriers[] = {
			VkMakeBufferBarrier2( 
				pDrawCmds->hndl,
				VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT ),
			VkMakeBufferBarrier2( 
				pCompactedDrawArgs->hndl,
				VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ),
			VkMakeBufferBarrier2( 
				pDrawCount->hndl,
				VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT ),
		};

		cmdBuff.CmdPipelineBufferBarriers( endCullBarriers );
	}
};

struct tonemapping_ctx
{
	std::shared_ptr<vk_buffer> pAverageLuminanceBuffer;
	std::shared_ptr<vk_buffer> pLuminanceHistogramBuffer;
	std::shared_ptr<vk_buffer> pAtomicWgCounterBuff;

	VkPipeline		compAvgLumPipe;
	VkPipeline		compTonemapPipe;

	u16 avgLumIdx;
	u16 atomicWgCounterIdx;
	u16 lumHistoIdx;

	void Init( vk_device_ctx& dc, VkPipelineLayout globalLayout, vk_descriptor_manager& descManager )
	{
		vk_shader2 avgLum = VkLoadShader2( "bin/SpirV/compute_AvgLuminanceCsMain.spirv", dc.device );
		compAvgLumPipe = VkMakeComputePipeline( 
			dc.device, 0, globalLayout, avgLum.shaderModule, {}, dc.waveSize, avgLum.entryPoint, "Pipeline_Comp_AvgLum" );

		vk_shader2 toneMapper = VkLoadShader2( "bin/SpirV/compute_TonemappingGammaCsMain.spirv", dc.device );
		compTonemapPipe = VkMakeComputePipeline( 
			dc.device, 0, globalLayout, toneMapper.shaderModule, {}, 
			dc.waveSize, toneMapper.entryPoint, "Pipeline_Comp_Tonemapping");

		VkBufferUsageFlags usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		pAverageLuminanceBuffer = std::make_shared<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_AvgLum",
			.usageFlags = usageFlags,
			.elemCount = 1,
			.stride = sizeof( float ),
			.usage = buffer_usage::GPU_ONLY } ) );
		avgLumIdx = VkAllocDescriptorIdx( descManager, vk_descriptor_info{ *pAverageLuminanceBuffer } );

		pAtomicWgCounterBuff = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_TonemappingAtomicWgCounter",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = 1,
			.stride = sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } ) );
		atomicWgCounterIdx = VkAllocDescriptorIdx(
			descManager, vk_descriptor_info{ *pAtomicWgCounterBuff } );

		pLuminanceHistogramBuffer = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_LumHisto",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = 4,
			.stride = sizeof( u64 ),
			.usage = buffer_usage::GPU_ONLY } ) ); 
		lumHistoIdx = VkAllocDescriptorIdx(
			descManager, vk_descriptor_info{ *pLuminanceHistogramBuffer } );
	}

	void AverageLuminancePass( 
		vk_command_buffer&  cmdBuff,
		float               dt, 
		u16				    hdrColSrcIdx,
		DirectX::XMUINT2	hdrTrgSize
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Averge Lum Pass", {} );
		cmdBuff.CmdBindPipelineAndBindlessDesc( compAvgLumPipe, VK_PIPELINE_BIND_POINT_COMPUTE );

		// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
		avg_luminance_info avgLumInfo = {
			.minLogLum = -10.0f,
			.invLogLumRange = 1.0f / 12.0f,
			.dt = dt
		};

		group_size groupSize = { 16, 16, 1 };
		DirectX::XMUINT3 numWorkGrs = { GroupCount( hdrTrgSize.x, groupSize.x ), GroupCount( hdrTrgSize.y, groupSize.y ), 1 };
		struct push_const
		{
			avg_luminance_info  avgLumInfo;
			uint				hdrColSrcIdx;
			uint				lumHistoIdx;
			uint				atomicWorkGrCounterIdx;
			uint				avgLumIdx;
		} pushConst = { avgLumInfo, hdrColSrcIdx, lumHistoIdx, atomicWgCounterIdx, avgLumIdx };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		cmdBuff.CmdDispatch( numWorkGrs );
	}

	void TonemappingGammaPass(
		vk_command_buffer& cmdBuff,
		u16                 hdrColIdx,
		u16                 sdrColIdx,
		DirectX::XMUINT2	hdrTrgSize
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Tonemapping Gamma Pass", {} );
		cmdBuff.CmdBindPipelineAndBindlessDesc( compTonemapPipe, VK_PIPELINE_BIND_POINT_COMPUTE );

		group_size groupSize = { 16, 16, 1 };
		DirectX::XMUINT3 numWorkGrs = { GroupCount( hdrTrgSize.x, groupSize.x ), GroupCount( hdrTrgSize.y, groupSize.y ), 1 };
		struct push_const
		{
			uint hdrColIdx;
			uint sdrColIdx;
			uint avgLumIdx;
		} pushConst = { hdrColIdx, sdrColIdx, avgLumIdx };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		cmdBuff.CmdDispatch( numWorkGrs );
	}
};

// TODO: add the backend here basically 
struct render_context
{
	imgui_context   imguiCtx;
	debug_context   dbgCtx;
	culling_ctx     cullingCtx;
	tonemapping_ctx tonemappingCtx;

	virtual_frame	vrtFrames[ renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED ];

	std::shared_ptr<vk_image> pColorTarget;
	std::shared_ptr<vk_image> pDepthTarget;

	u16 colSrv;
	u16 depthSrv;

	// TODO: move to appropriate technique/context
	VkPipeline      gfxZPrepass;
	VkPipeline		gfxPipeline;
	VkPipeline		gfxMeshletPipeline;
	VkPipeline		gfxMergedPipeline;

	VkPipeline		compHiZPipeline;

	VkSemaphore     timelineSema;
	u64				vFrameIdx = 0;
	u8				framesInFlight;

	// TODO: move to appropriate technique/context
	std::shared_ptr<vk_image> pHiZTarget;

	VkImageView hiZMipViews[ MAX_MIP_LEVELS ];

	VkSampler quadMinSampler;
	VkSampler pbrSampler;

	u16 hizSrv;
	
	u16 hizMipUavs[ MAX_MIP_LEVELS ];
	u16 quadMinSamplerIdx;
	u16 pbrSamplerIdx;

	u16 swapchainUavs[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
};

static render_context rndCtx;

// TODO: move to render_context ?
struct vk_backend
{
	std::unique_ptr<vk_device_ctx> pDc;
	vk_descriptor_manager descManager;
	vk_instance inst;
	VkSurfaceKHR surf;
	VkPipelineLayout globalLayout;
};

static vk_backend vk;

// TODO: separate from render_context
void VkBackendInit( uintptr_t hInst, uintptr_t hWnd )
{
	vk.inst = VkMakeInstance();

	vk.surf = VkMakeWinSurface( vk.inst.hndl, ( HINSTANCE ) hInst, ( HWND ) hWnd );
	vk.pDc = std::make_unique<vk_device_ctx>( VkMakeDeviceContext( vk.inst.hndl, vk.surf, renderCfg ) );

	rndCtx.framesInFlight = renderCfg.framesInFlightCount;
	for( u64 vfi = 0; vfi < rndCtx.framesInFlight; ++vfi )
	{
		rndCtx.vrtFrames[ vfi ] = VkCreateVirtualFrame( *vk.pDc );
	}
	VkSemaphoreTypeCreateInfo timelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = rndCtx.vFrameIdx = 0,
	};
	VkSemaphoreCreateInfo timelineSemaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineInfo };
	VK_CHECK( vkCreateSemaphore( vk.pDc->device, &timelineSemaInfo, 0, &rndCtx.timelineSema ) );

	vk.descManager = VkMakeDescriptorManager( vk.pDc->device, vk.pDc->gpuProps );
	vk.globalLayout = VkMakeGlobalPipelineLayout( vk.pDc->device, vk.pDc->gpuProps, vk.descManager );
	VkDbgNameObj( vk.globalLayout, vk.pDc->device, "Vk_Pipeline_Layout_Global" );

	{
		vk_shader vertZPre = VkLoadShader( "Shaders/v_z_prepass.vert.spv", vk.pDc->device );
		rndCtx.gfxZPrepass = VkMakeGfxPipeline( 
			vk.pDc->device, 0, vk.globalLayout, vertZPre.module, 0, 0, 0, renderCfg.desiredDepthFormat, {} );

		vkDestroyShaderModule( vk.pDc->device, vertZPre.module, 0 );
	}
	{
		vk_shader vertBox = VkLoadShader( "Shaders/box_meshlet_draw.vert.spv", vk.pDc->device );
		vk_shader normalCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", vk.pDc->device );

		vk_gfx_pipeline_state lineDrawPipelineState = {
			.polyMode = VK_POLYGON_MODE_LINE,
			.cullFlags = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
			.depthWrite = VK_FALSE,
			.depthTestEnable = VK_FALSE,
			.blendCol = VK_FALSE
		};

		dbgDrawProgram = VkMakePipelineProgram( 
			vk.pDc->device, vk.pDc->gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertBox, &normalCol }, vk.descManager.setLayout );
		gfxDrawIndirDbg = VkMakeGfxPipeline(
			vk.pDc->device, 0, dbgDrawProgram.pipeLayout, 
			vertBox.module, normalCol.module, 
			&renderCfg.desiredColorFormat, 1, VK_FORMAT_UNDEFINED,
			lineDrawPipelineState );

		vkDestroyShaderModule( vk.pDc->device, vertBox.module, 0 );
		vkDestroyShaderModule( vk.pDc->device, normalCol.module, 0 );
	}
	rndCtx.cullingCtx.Init( *vk.pDc, vk.globalLayout );
	rndCtx.tonemappingCtx.Init( *vk.pDc, vk.globalLayout, vk.descManager );
	{
		vk_shader vtxMerged = VkLoadShader( "Shaders/vtx_merged.vert.spv", vk.pDc->device );
		vk_shader fragPBR = VkLoadShader( "Shaders/pbr.frag.spv", vk.pDc->device );
		vk_gfx_pipeline_state opaqueState = {};

		rndCtx.gfxMergedPipeline = VkMakeGfxPipeline(
			vk.pDc->device, 0, vk.globalLayout, 
			vtxMerged.module, fragPBR.module, 
			&renderCfg.desiredColorFormat, 1, renderCfg.desiredDepthFormat, 
			opaqueState );
		VkDbgNameObj( rndCtx.gfxMergedPipeline, vk.pDc->device, "Pipeline_Gfx_Merged" );

		vkDestroyShaderModule( vk.pDc->device, vtxMerged.module, 0 );
		vkDestroyShaderModule( vk.pDc->device, fragPBR.module, 0 );
	}
	{
		vk_shader vertMeshlet = VkLoadShader( "Shaders/meshlet.vert.spv", vk.pDc->device );
		vk_shader fragCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", vk.pDc->device );
		rndCtx.gfxMeshletPipeline = VkMakeGfxPipeline(
			vk.pDc->device, 0, vk.globalLayout, vertMeshlet.module, fragCol.module, 
			&renderCfg.desiredColorFormat, 1, renderCfg.desiredDepthFormat, {} );
		VkDbgNameObj( rndCtx.gfxMeshletPipeline, vk.pDc->device, "Pipeline_Gfx_MeshletDraw" );

		vkDestroyShaderModule( vk.pDc->device, vertMeshlet.module, 0 );
		vkDestroyShaderModule( vk.pDc->device, fragCol.module, 0 );
	}
	{
		vk_shader2 downsampler = VkLoadShader2( "bin/SpirV/compute_Pow2DownSamplerCsMain.spirv", vk.pDc->device );
		rndCtx.compHiZPipeline = VkMakeComputePipeline( 
			vk.pDc->device, 0, vk.globalLayout, downsampler.shaderModule, {}, 
			vk.pDc->waveSize, downsampler.entryPoint, "Pipeline_Comp_HiZ" );
	}

	rndCtx.dbgCtx.Init( *vk.pDc, vk.globalLayout, renderCfg );
	rndCtx.dbgCtx.InitData( *vk.pDc, 1 * KB, 1 * KB );

	rndCtx.imguiCtx.Init( *vk.pDc );
}

inline static void
DrawIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	const VkRenderingAttachmentInfo* pColInfo,
	const VkRenderingAttachmentInfo* pDepthInfo,
	const vk_buffer&      drawCmds,
	VkBuffer				drawCmdCount,
	const vk_program&       program,
	const mat4&             viewProjMat,
	const VkRect2D& scissor
){
	vk_scoped_label label = { cmdBuff,"Draw Indirect Pass",{} };

	VkCmdBeginRendering( cmdBuff, pColInfo, pColInfo ? 1 : 0, pDepthInfo, scissor, 1 );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	struct { mat4 viewProj; vec4 color; u64 cmdAddr; u64 transfAddr; u64 meshletAddr; } push = {
		viewProjMat, { 255,0,0,0 }, drawCmds.devicePointer, instDescBuff.devicePointer, meshletBuff.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( push ), &push );

	u32 maxDrawCnt = drawCmds.sizeInBytes / sizeof( draw_indirect );
	vkCmdDrawIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_indirect, cmd ), drawCmdCount, 0, maxDrawCnt, sizeof( draw_indirect ) );

	vkCmdEndRendering( cmdBuff );
}

inline static void
DrawIndexedIndirectMerged(
	vk_command_buffer		cmdBuff,
	VkPipeline	            vkPipeline,
	const vk_rendering_info& renderingInfo,
	const vk_buffer&      indexBuff,
	VkIndexType           indexType,
	const vk_buffer&      drawCmds,
	const vk_buffer&      drawCount,
	u32 maxDrawCount,
	const void* pPushData,
	u64 pushDataSize
) {
	vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Draw Indexed Indirect Pass", {} );

	vk_scoped_dynamic_renderpass dynamicRendering = cmdBuff.CmdIssueDynamicScopedRenderPass( 
		std::data( renderingInfo.colorAttachments ),
		std::size( renderingInfo.colorAttachments ), 
		renderingInfo.pDepthAttachment, 
		renderingInfo.scissor, 1 );

	vkCmdSetScissor( cmdBuff.hndl, 0, 1, &renderingInfo.scissor );
	vkCmdSetViewport( cmdBuff.hndl, 0, 1, &renderingInfo.viewport );
	
	cmdBuff.CmdBindPipelineAndBindlessDesc( vkPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS );
	
	cmdBuff.CmdPushConstants( pPushData, pushDataSize );
	cmdBuff.CmdDrawIndexedIndirectCount( indexBuff, indexType, drawCmds, drawCount, maxDrawCount );
}

#if 0
// TODO: must remake single pass
inline static void
DepthPyramidPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	u64						mipLevelsCount,
	VkSampler				quadMinSampler,
	VkImageView				( &depthMips )[ MAX_MIP_LEVELS ],
	const vk_image&			depthTarget,
	const vk_image&			depthPyramid,
	const vk_program&		program 
){
	static_assert( 0 );
	assert( 0 );
	u32 dispatchGroupX = ( ( depthTarget.width + 63 ) >> 6 );
	u32 dispatchGroupY = ( ( depthTarget.height + 63 ) >> 6 );

	downsample_info dsInfo = {};
	dsInfo.mips = mipLevelsCount;
	dsInfo.invRes.x = 1.0f / float( depthTarget.width );
	dsInfo.invRes.y = 1.0f / float( depthTarget.height );
	dsInfo.workGroupCount = dispatchGroupX * dispatchGroupY;


	VkImageMemoryBarrier depthReadBarrier = VkMakeImgBarrier( depthTarget.hndl,
															  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
															  VK_ACCESS_SHADER_READ_BIT,
															  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
															  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															  VK_IMAGE_ASPECT_DEPTH_BIT,
															  0, 0 );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,  
						  1, &depthReadBarrier );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	std::vector<vk_descriptor_info> depthPyramidDescs( MAX_MIP_LEVELS + 3 );
	depthPyramidDescs[ 0 ] = { 0, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	depthPyramidDescs[ 1 ] = { quadMinSampler, 0, VK_IMAGE_LAYOUT_GENERAL };
	depthPyramidDescs[ 2 ] = { depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size };
	for( u64 i = 0; i < depthPyramid.mipCount; ++i )
	{
		depthPyramidDescs[ i + 3 ] = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
	}

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, std::data( depthPyramidDescs ) );

	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( dsInfo ), &dsInfo );

	vkCmdDispatch( cmdBuff, dispatchGroupX, dispatchGroupY, 1 );

	VkImageMemoryBarrier depthWriteBarrier = VkMakeImgBarrier( depthTarget.hndl,
															   VK_ACCESS_SHADER_READ_BIT,
															   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
															   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
															   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
															   VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0 );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,
						  1, &depthWriteBarrier );
}
#endif

inline static void
DepthPyramidMultiPass(
	vk_command_buffer		cmdBuff,
	VkPipeline				vkPipeline,
	const vk_image&			depthTarget,
	u16                     depthIdx,
	const vk_image&			depthPyramid,
	u16                     hiZReadIdx,
	const u16*              hiZMipWriteIndices,
	u16                     samplerIdx,
	VkPipelineLayout        pipelineLayout,
	group_size              grSz
) {
	vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "HiZ Multi Pass", {} );

	VkImageMemoryBarrier2 hizBeginBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_DEPTH_BIT ),

		VkMakeImageBarrier2( 
			depthPyramid.hndl,
			0,0,
			VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};

	cmdBuff.CmdPipelineImageBarriers( hizBeginBarriers );

	cmdBuff.CmdBindPipelineAndBindlessDesc( vkPipeline, VK_PIPELINE_BIND_POINT_COMPUTE );

	VkMemoryBarrier2 executionBarrier[] = { {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
	} };

	uint mipLevel = 0;
	uint srcImg = depthIdx;
	for( u64 i = 0; i < depthPyramid.mipCount; ++i )
	{
		if( i > 0 )
		{
			mipLevel = i - 1;
			srcImg = hiZReadIdx;
		}
		uint dstImg = hiZMipWriteIndices[ i ];

		u32 levelWidth = std::max( 1u, u32( depthPyramid.width ) >> i );
		u32 levelHeight = std::max( 1u, u32( depthPyramid.height ) >> i );

		vec2 reduceData{ ( float ) levelWidth, ( float ) levelHeight };

		struct push_const
		{
			vec2 reduce;
			uint samplerIdx;
			uint srcImgIdx;
			uint mipLevel;
			uint dstImgIdx;

			push_const(vec2 r, uint s, uint src, uint mip, uint dst)
				: reduce(r), samplerIdx(s), srcImgIdx(src), mipLevel(mip), dstImgIdx(dst) {}
		};
		push_const pushConst{ vec2{(float)levelWidth, (float)levelHeight}, samplerIdx, srcImg, mipLevel, dstImg };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		
		u32 dispatchX = GroupCount( levelWidth, grSz.x );
		u32 dispatchY = GroupCount( levelHeight, grSz.y );
		vkCmdDispatch( cmdBuff.hndl, dispatchX, dispatchY, 1 );

		cmdBuff.CmdPipelineMemoryBarriers( executionBarrier );
	}

	// TODO: do we need ?
	VkImageMemoryBarrier2 hizEndBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			VK_IMAGE_ASPECT_DEPTH_BIT ),
			//VkMakeImageBarrier2(
			//	depthPyramid.hndl,
			//	VK_ACCESS_2_SHADER_WRITE_BIT,
			//	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//	VK_ACCESS_2_SHADER_READ_BIT,
			//	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//	VK_IMAGE_LAYOUT_GENERAL,
			//	VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			//	VK_IMAGE_ASPECT_COLOR_BIT )
	};

	cmdBuff.CmdPipelineImageBarriers( hizEndBarriers );
}


// TODO: enforce some clearOp ---> clearVals params correctness ?
inline static VkRenderingAttachmentInfo VkMakeAttachemntInfo(
	VkImageView view,
	VkAttachmentLoadOp       loadOp,
	VkAttachmentStoreOp      storeOp,
	VkClearValue             clearValue
) {
	return {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		.loadOp = loadOp,
		.storeOp = storeOp,
		.clearValue = ( loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ) ? clearValue : VkClearValue{},
	};
}

void VkInitGlobalResources( vk_device_ctx& dc, render_context& rndCtx, vk_descriptor_manager& descManager )
{
	if( rndCtx.pHiZTarget == nullptr )
	{
		u16 squareDim = 512;
		u8 hiZMipCount = GetImgMipCountForPow2( squareDim, squareDim, MAX_MIP_LEVELS );

		assert( MAX_MIP_LEVELS >= hiZMipCount );

		constexpr VkImageUsageFlags hiZUsg =
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		image_info hiZInfo = {
			.name = "Img_HiZ",
			.format = VK_FORMAT_R32_SFLOAT,
			.usg = hiZUsg,
			.width = squareDim,
			.height = squareDim,
			.layerCount = 1,
			.mipCount = hiZMipCount
		};

		rndCtx.pHiZTarget = std::make_shared<vk_image>( dc.CreateImage( hiZInfo ) );

		rndCtx.hizSrv = VkAllocDescriptorIdx( 
			descManager, vk_descriptor_info{ rndCtx.pHiZTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		for( u64 i = 0; i < rndCtx.pHiZTarget->mipCount; ++i )
		{
			rndCtx.hiZMipViews[ i ] = VkMakeImgView( 
				dc.device, rndCtx.pHiZTarget->hndl, hiZInfo.format, i, 1,
				VK_IMAGE_VIEW_TYPE_2D, 0, hiZInfo.layerCount );
			rndCtx.hizMipUavs[ i ] = VkAllocDescriptorIdx( 
				descManager, vk_descriptor_info{ rndCtx.hiZMipViews[ i ], VK_IMAGE_LAYOUT_GENERAL } );
		}

		rndCtx.quadMinSampler =
			VkMakeSampler( dc.device, VK_SAMPLER_REDUCTION_MODE_MIN, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE );
		
		rndCtx.quadMinSamplerIdx = VkAllocDescriptorIdx( descManager, vk_descriptor_info{ rndCtx.quadMinSampler } );
	}
	if( rndCtx.pDepthTarget == nullptr )
	{
		constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_info info = {
			.name = "Img_DepthTarget",
			.format = renderCfg.desiredDepthFormat,
			.usg = usgFlags,
			.width = dc.sc.width,
			.height = dc.sc.height,
			.layerCount = 1,
			.mipCount = 1,
		};
		rndCtx.pDepthTarget = std::make_shared<vk_image>( dc.CreateImage( info ) );

		rndCtx.depthSrv = VkAllocDescriptorIdx( 
			descManager, vk_descriptor_info{ rndCtx.pDepthTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
	}
	if( rndCtx.pColorTarget == nullptr )
	{
		constexpr VkImageUsageFlags usgFlags =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image_info info = {
			.name = "Img_ColorTarget",
			.format = renderCfg.desiredColorFormat,
			.usg = usgFlags,
			.width = dc.sc.width,
			.height = dc.sc.height,
			.layerCount = 1,
			.mipCount = 1,
		};
		rndCtx.pColorTarget = std::make_shared<vk_image>( dc.CreateImage( info ) );

		rndCtx.colSrv = VkAllocDescriptorIdx( 
			descManager, vk_descriptor_info{ rndCtx.pColorTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
	
		rndCtx.pbrSampler = 
			VkMakeSampler( dc.device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );

		rndCtx.pbrSamplerIdx = VkAllocDescriptorIdx( descManager, vk_descriptor_info{ rndCtx.pbrSampler } );
	}

	for( u64 scImgIdx = 0; scImgIdx < dc.sc.imgCount; ++scImgIdx )
	{
		rndCtx.swapchainUavs[ scImgIdx ] = VkAllocDescriptorIdx( 
			descManager, vk_descriptor_info{ dc.sc.imgViews[ scImgIdx ], VK_IMAGE_LAYOUT_GENERAL } );
	}
}

struct drak_file_viewer
{
	const drak_file_footer const* fileFooter;
	std::span<const u8> binaryData;

	drak_file_viewer( std::span<const u8> data ) : binaryData{ data }
	{
		assert( std::size( data ) );
		//binaryData = SysReadFile( drakPath );
		if( std::size( binaryData ) == 0 )
		{
			assert( 0 && "No valid file" );
			//std::vector<u8> fileData = SysReadFile( glbPath );
			//CompileGlbAssetToBinary( fileData, binaryData );
			// TODO: does this override ?
			//SysWriteToFile( drakPath, std::data( binaryData ), std::size( binaryData ) );
		}
		fileFooter = ( drak_file_footer* ) ( std::data( binaryData ) + std::size( binaryData ) - sizeof( drak_file_footer ) );
		assert( std::strcmp( "DRK", fileFooter->magik ) == 0 );
	}

	std::span<const mesh_desc> GetMeshes() const
	{
		return { (mesh_desc*) ( std::data( binaryData ) + fileFooter->meshesByteRange.offset ),
			fileFooter->meshesByteRange.size / sizeof( mesh_desc ) };
	}

	std::span<const u8> GetVertexMegaBuffer() const
	{
		return { std::data( binaryData ) + fileFooter->vtxByteRange.offset, fileFooter->vtxByteRange.size };
	}

	std::span<const u8> GetIndexMegaBuffer() const
	{
		return { std::data( binaryData ) + fileFooter->idxByteRange.offset, fileFooter->idxByteRange.size };
	}

	std::span<const meshlet> GetMeshletsMegaBuffer() const
	{
		return { ( meshlet* ) ( std::data( binaryData ) + fileFooter->mletsByteRange.offset ),
			fileFooter->mletsByteRange.size / sizeof( meshlet ) };
	}

	std::span<const u8> GetMeshletDataMegaBuffer() const
	{
		return { std::data( binaryData ) + fileFooter->mletsDataByteRange.offset, fileFooter->mletsDataByteRange.size };
	}

	std::span<const material_data> GetMaterials() const
	{
		return { ( material_data* ) ( std::data( binaryData ) + fileFooter->mtrlsByteRange.offset ),
			fileFooter->mtrlsByteRange.size / sizeof( material_data ) };
	}

	std::span<const image_metadata> GetImagesMetadata() const
	{
		return { ( image_metadata* ) ( std::data( binaryData ) + fileFooter->imgsByteRange.offset ),
			fileFooter->imgsByteRange.size / sizeof( image_metadata ) };
	}

	std::span<const u8> GetTextureBinaryData( const image_metadata& meta ) const
	{
		const u8* pTexBinData = std::data( binaryData ) + fileFooter->texBinByteRange.offset + meta.texBinRange.offset;
		return { pTexBinData, meta.texBinRange.size };
	}

	u64 GetTextureBinaryDataTotalSize() const
	{
		return fileFooter->texBinByteRange.size;
	}
};

struct renderer_geometry
{
	// TODO: use more vtxBuffers ?
	std::shared_ptr<vk_buffer> pVertexBuffer;
	std::shared_ptr<vk_buffer> pIdxBuffer;
	// NOTE: will hold transforms
	std::shared_ptr<vk_buffer> pNodeBuffer;
	std::shared_ptr<vk_buffer> pInstanceBounds;
	std::shared_ptr<vk_buffer> pMeshes;
	std::shared_ptr<vk_buffer> pMaterials;
	std::shared_ptr<vk_buffer> pLights;

	// TODO: fix meshlet stuff
	std::shared_ptr<vk_buffer> pMeshletBuffer;
	std::shared_ptr<vk_buffer> pMeshletIdxBuffer;

	std::vector<std::shared_ptr<vk_image>> pTextures;

	void UploadMaterials( 
		vk_command_buffer&      cmdBuff, 
		vk_device_ctx&          dc, 
		const drak_file_viewer& viewDrkFile, 
		u64                     currentFrameId 
	) {
		std::vector<VkHostImageLayoutTransitionInfo> imageTransitions;
		// NOTE: need this to append
		const u64 texOffset = std::size( pTextures );

		auto imgMetadataSpan = viewDrkFile.GetImagesMetadata();
		for( const image_metadata& meta : imgMetadataSpan )
		{
			const image_info info = GetImageInfoFromMetadata( 
				meta, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT );
			auto img = std::make_shared<vk_image>( dc.CreateImage( info ) );
			pTextures.push_back( img );

			VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( img->format );
			VkHostImageLayoutTransitionInfo hostImgLayoutTransitionInfo = {
				.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO,
				.image = img->hndl,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.subresourceRange = {
					.aspectMask = aspectFlags,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
			    },
			};
		}
		dc.TransitionImageLayout( std::data( imageTransitions ), std::size( imageTransitions ) );

		for( u64 i = 0; i < std::size( imgMetadataSpan ); ++i )
		{
			const image_metadata& meta = imgMetadataSpan[ i ];
			auto texBinSpan = viewDrkFile.GetTextureBinaryData( meta );
			dc.CopyMemoryToImage( *pTextures[ i ], std::data( texBinSpan ) );
		}

		// NOTE: we assume materials have an abs idx for the textures
		std::vector<material_data> mtrls = {};
		for( const material_data& m : viewDrkFile.GetMaterials() )
		{
			mtrls.push_back( m );
			material_data& refM = mtrls[ std::size( mtrls ) - 1 ];

			const auto& mBaseCol = pTextures[ m.baseColIdx + texOffset ];
			const auto& mNormalMap = pTextures[ m.normalMapIdx + texOffset ];
			const auto& mOccRoughMetal = pTextures[ m.occRoughMetalIdx + texOffset ];

			refM.baseColIdx = VkAllocDescriptorIdx( 
				vk.descManager, vk_descriptor_info{ mBaseCol->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
			refM.normalMapIdx = VkAllocDescriptorIdx( 
				vk.descManager, vk_descriptor_info{ mNormalMap->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
			refM.occRoughMetalIdx = VkAllocDescriptorIdx( 
				vk.descManager, vk_descriptor_info{ mOccRoughMetal->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL });
		}
		constexpr VkBufferUsageFlags usg =
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		pMaterials = std::make_shared<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_Mtrls",
			.usageFlags = usg,
			.elemCount = ( u32 ) std::size( mtrls ),
			.stride = sizeof( decltype( mtrls )::value_type ),
			.usage = buffer_usage::GPU_ONLY
		} ) );

		StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, 
			CastSpanAsU8ReadOnly( std::span<material_data>{mtrls} ), materialsBuff, currentFrameId );
		
		VkBufferMemoryBarrier2 buffBarriers[] = {
			VkMakeBufferBarrier2(
				materialsBuff.hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT )
		};
		cmdBuff.CmdPipelineBufferBarriers( buffBarriers );
	}

	void UploadGeometry(
		vk_command_buffer&      cmdBuff, 
		vk_device_ctx&          dc, 
		const drak_file_viewer& viewDrkFile, 
		u64                     currentFrameId 
	) {
		std::vector<VkBufferMemoryBarrier2> buffBarriers;

		auto vtxView = viewDrkFile.GetVertexMegaBuffer();
		pVertexBuffer = std::make_shared<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_Vtx",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) std::size( vtxView ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } ) );

		StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, vtxView, *pVertexBuffer, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2( 
			pVertexBuffer->hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ) );

		auto idxView = viewDrkFile.GetVertexMegaBuffer();
		pIdxBuffer = std::make_shared<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_Idx",
			.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = ( u32 ) std::size( idxView ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } ) );

		StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, idxView, *pIdxBuffer, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2( 
			pIdxBuffer->hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_INDEX_READ_BIT, 
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT ) );

		pMeshes = std::make_shared<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_MeshDesc",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( 1),//BYTE_COUNT( meshes ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } ) ); 

		//StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, CastSpanAsU8ReadOnly( meshes ), *pMeshes, currentFrameId );	 

		buffBarriers.push_back( VkMakeBufferBarrier2(
			pMeshes->hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ) );

		auto mletStaged = stagingManager.GetStagingBufferAndCopyHostData( dc, viewDrkFile.GetMeshes(), currentFrameId );
		pMeshletBuffer = std::make_shared<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_Meshlets",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = mletStaged->sizeInBytes,
			.stride = sizeof( meshlet ),
			.usage = buffer_usage::GPU_ONLY } ) );  

		cmdBuff.CmdCopyBuffer( *mletStaged, *pMeshletBuffer, { 0, 0, mletStaged->sizeInBytes } );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			pMeshletBuffer->hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) );

		meshletDataBuff = dc.CreateBuffer( {
			.name = "Buff_MeshletData",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT ,
	        .elemCount = (u32) ( 10),//std::size( mletDataView ) ),
	        .stride = 1,
	        .usage = buffer_usage::GPU_ONLY } );  

		//StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, mletDataView, meshletDataBuff, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletDataBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) );
	}

	void UploadSceneData(

	) {

	}
};

static inline void VkUploadResources( 
	vk_device_ctx& dc,
	staging_manager& stagingManager,
	vk_command_buffer& cmdBuff, 
	entities_data& entities, 
	u64 currentFrameId
) {
	std::vector<u8> binaryData;
	// TODO: add renderable_instances
	// TODO: extra checks and stuff ?
	// TODO: ensure resources of the same type are contiguous ?
	{
		binaryData = SysReadFile( drakPath );
		if( std::size( binaryData ) == 0 )
		{
			std::vector<u8> fileData = SysReadFile( glbPath );
			CompileGlbAssetToBinary( fileData, binaryData );
			// TODO: does this override ?
			SysWriteToFile( drakPath, std::data( binaryData ), std::size( binaryData ) );
		}
	}

	const drak_file_footer& fileFooter =
		*( drak_file_footer* ) ( std::data( binaryData ) + std::size( binaryData ) - sizeof( drak_file_footer ) );
	//assert( "DRK"sv == fileFooter.magik  );

	const std::span<mesh_desc> meshes = { 
		(mesh_desc*) ( std::data( binaryData ) + fileFooter.meshesByteRange.offset ),
		fileFooter.meshesByteRange.size / sizeof( mesh_desc ) };


	std::srand( randSeed );

	std::vector<instance_desc> instDesc = SpawnRandomInstances( { std::data( meshes ),std::size( meshes ) }, drawCount, 1, sceneRad );
	std::vector<light_data> lights = SpawnRandomLights( lightCount, sceneRad * 0.75f );

	assert( std::size( instDesc ) < u16( -1 ) );


	for( const instance_desc& ii : instDesc )
	{
		const mesh_desc& m = meshes[ ii.meshIdx ];
		entities.transforms.push_back( ii.localToWorld );
		entities.instAabbs.push_back( { m.aabbMin, m.aabbMax } );
	}


	std::vector<VkBufferMemoryBarrier2> buffBarriers;
	
	{
		lightsBuff = dc.CreateBuffer( {
			.name = "Buff_Lights",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( BYTE_COUNT( lights ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );  

		StagingManagerUploadBuffer(
			dc, stagingManager, cmdBuff, CastSpanAsU8ReadOnly( std::span<light_data>{lights} ), lightsBuff, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			lightsBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ) );
	}
	{
		instDescBuff = dc.CreateBuffer( {
			.name = "Buff_InstDescs",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( BYTE_COUNT( instDesc ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );  

		StagingManagerUploadBuffer(
			dc, stagingManager, cmdBuff, 
			CastSpanAsU8ReadOnly( std::span<instance_desc>{instDesc} ), instDescBuff, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			instDescBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ) );
	}
}

// TODO: in and out data
void HostFrames( const frame_data& frameData, gpu_data& gpuData )
{
	const u64 currentFrameIdx = rndCtx.vFrameIdx++;
	const u64 currentFrameBufferedIdx = currentFrameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;
	virtual_frame& thisVFrame = rndCtx.vrtFrames[ currentFrameBufferedIdx ];

	VkSemaphoreWaitInfo waitInfo = { 
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &rndCtx.timelineSema,
		.pValues = &currentFrameIdx,
	};
	VK_CHECK( VK_INTERNAL_ERROR( vkWaitSemaphores( vk.pDc->device, &waitInfo, UINT64_MAX ) > VK_TIMEOUT ) );

	VK_CHECK( vkResetCommandPool( vk.pDc->device, thisVFrame.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );

	// TODO: 
	if( currentFrameIdx < VK_MAX_FRAMES_IN_FLIGHT_ALLOWED )
	{
		thisVFrame.pViewData = std::make_shared<vk_buffer>( vk.pDc->CreateBuffer( {
			.name = "Buff_VirtualFrame_ViewBuff",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.elemCount = std::size( frameData.views ),
			.stride = sizeof( view_data ),
			.usage = buffer_usage::HOST_VISIBLE
		} ) );
		thisVFrame.viewDataIdx = VkAllocDescriptorIdx( vk.descManager, vk_descriptor_info{ *thisVFrame.pViewData } );
	}
	assert( thisVFrame.pViewData->sizeInBytes == BYTE_COUNT( frameData.views ) );
	std::memcpy( thisVFrame.pViewData->hostVisible, std::data( frameData.views ), BYTE_COUNT( frameData.views ) );
	
	vk_command_buffer thisFrameCmdBuffer = { thisVFrame.cmdBuff, vk.globalLayout, vk.descManager.set };

	std::vector<vk_descriptor_write> vkDescUpdateCache;
	static bool initResources = false;
	if( !initResources )
	{
		VkInitGlobalResources( *vk.pDc, rndCtx, vk.descManager );

		VkUploadResources( *vk.pDc, stagingManager, thisFrameCmdBuffer, entities, currentFrameIdx );

		u32 instCount = instDescBuff.sizeInBytes / sizeof( instance_desc );
		u32 mletCount = meshletBuff.sizeInBytes / sizeof( meshlet );
		rndCtx.cullingCtx.InitSceneDependentData( *vk.pDc, instCount, mletCount * instCount );

		rndCtx.dbgCtx.UploadDebugGeometry();

		rndCtx.imguiCtx.InitResources( *vk.pDc, renderCfg.desiredSwapchainFormat );

		thisFrameCmdBuffer.CmdFillVkBuffer( *rndCtx.tonemappingCtx.pAverageLuminanceBuffer, 0u );

		VkBufferMemoryBarrier2 initBuffersBarriers[] = {
			VkMakeBufferBarrier2( rndCtx.tonemappingCtx.pAverageLuminanceBuffer->hndl,
								  VK_ACCESS_2_TRANSFER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		};

		//VkImageMemoryBarrier2 initBarriers[] = {
		//	VkMakeImageBarrier2( 
		//		rndCtx.imguiCtx.fontsImg.hndl, 0, 0,
		//		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		//		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		//		VK_IMAGE_LAYOUT_UNDEFINED,
		//		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		//		VK_IMAGE_ASPECT_COLOR_BIT )
		//};

		thisFrameCmdBuffer.CmdPipelineBufferBarriers( initBuffersBarriers );
		initResources = true;
	}

	VkDescriptorManagerFlushUpdates( vk.descManager, vk.pDc->device );

	const vk_image& depthTarget = *rndCtx.pDepthTarget;
	const vk_image& depthPyramid = *rndCtx.pHiZTarget;
	const vk_image& colorTarget = *rndCtx.pColorTarget;

	auto depthWrite = VkMakeAttachemntInfo( depthTarget.view, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto depthRead = VkMakeAttachemntInfo( depthTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorWrite = VkMakeAttachemntInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorRead = VkMakeAttachemntInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );

	VkViewport viewport = VkGetSwapchainViewport( vk.pDc->sc );
	VkRect2D scissor = VkGetSwapchianScissor( vk.pDc->sc );

	u32 instCount = instDescBuff.sizeInBytes / sizeof( instance_desc );
	u32 mletCount = meshletBuff.sizeInBytes / sizeof( meshlet );
	u32 meshletUpperBound = instCount * mletCount;

	DirectX::XMMATRIX t = DirectX::XMMatrixMultiply( 
		DirectX::XMMatrixScaling( 180.0f, 100.0f, 60.0f ), DirectX::XMMatrixTranslation( 20.0f, -10.0f, -60.0f ) );
	DirectX::XMFLOAT4X4A debugOcclusionWallTransf;
	DirectX::XMStoreFloat4x4A( &debugOcclusionWallTransf, t );

	DirectX::XMUINT2 colorTargetSize = { colorTarget.width, colorTarget.height };

	VkResetGpuTimer( thisVFrame.cmdBuff, thisVFrame.gpuTimer );
	u32 imgIdx;
	{
		vk_time_section timePipeline = { thisVFrame.cmdBuff, thisVFrame.gpuTimer.queryPool, 0 };
		rndCtx.cullingCtx.Execute( thisFrameCmdBuffer, depthPyramid, instCount, thisVFrame.viewDataIdx, 
								   rndCtx.hizSrv, rndCtx.quadMinSamplerIdx, false );

		VkImageMemoryBarrier2 acquireAttachmentsBarriers[] = {
			VkMakeImageBarrier2(
				depthTarget.hndl,
				0, 0,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_IMAGE_ASPECT_DEPTH_BIT ),
			VkMakeImageBarrier2(
				colorTarget.hndl,
				0, 
				0, //VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ),
		};
		thisFrameCmdBuffer.CmdPipelineBarriers( {}, acquireAttachmentsBarriers );

		struct {
			u64 vtxAddr, transfAddr, compactedArgsAddr, mtrlsAddr, lightsAddr; u32 camIdx, samplerIdx;
		} shadingPush = { 
				.vtxAddr = globVertexBuff.devicePointer, 
				.transfAddr = instDescBuff.devicePointer, 
				.compactedArgsAddr = rndCtx.cullingCtx.pCompactedDrawArgs->devicePointer,
				.mtrlsAddr = materialsBuff.devicePointer, 
				.lightsAddr = lightsBuff.devicePointer,
				.camIdx = thisVFrame.viewDataIdx,
				.samplerIdx = rndCtx.pbrSamplerIdx
		};

		VkRenderingAttachmentInfo attInfos[] = { colorWrite };
		vk_rendering_info colorPassInfo = {
			.viewport = viewport,
			.scissor = scissor,
			.colorAttachments = attInfos,
			.pDepthAttachment = &depthWrite
		};

		DrawIndexedIndirectMerged(
			thisFrameCmdBuffer,
			rndCtx.gfxMergedPipeline,
			colorPassInfo,
			indexBuff,
			VK_INDEX_TYPE_UINT32,
			*rndCtx.cullingCtx.pDrawCmds,
			*rndCtx.cullingCtx.pDrawCount,
			meshletUpperBound,
			&shadingPush,
			sizeof(shadingPush)
		);

		vk_rendering_info zDgbInfo = {
			.viewport = viewport,
			.scissor = scissor,
			.colorAttachments = {},
			.pDepthAttachment = &depthRead
		};
		rndCtx.dbgCtx.DrawCPU( thisFrameCmdBuffer, zDgbInfo, "Draw Occluder-Depth", debug_draw_type::TRIANGLE, 
			thisVFrame.pViewData->devicePointer, 0, debugOcclusionWallTransf, 0 );

		DepthPyramidMultiPass(
			thisFrameCmdBuffer,
			rndCtx.compHiZPipeline,
			depthTarget,
			rndCtx.depthSrv,
			depthPyramid,
			rndCtx.hizSrv,
			rndCtx.hizMipUavs,
			rndCtx.quadMinSamplerIdx,
			vk.globalLayout,
			{32,32,1}
		 );

		VkBufferMemoryBarrier2 clearDrawCountBarrier[] = { 
			VkMakeBufferBarrier2(
				rndCtx.cullingCtx.pDrawCount->hndl,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT ),
			VkMakeBufferBarrier2( 
				rndCtx.cullingCtx.pAtomicWgCounter->hndl,
				VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT
			),
		};

		thisFrameCmdBuffer.CmdPipelineBufferBarriers( clearDrawCountBarrier );

		rndCtx.cullingCtx.Execute( thisFrameCmdBuffer, depthPyramid, instCount, thisVFrame.viewDataIdx, 
			rndCtx.hizSrv, rndCtx.quadMinSamplerIdx, true );

		colorPassInfo.pDepthAttachment = &depthRead;
		attInfos[ 0 ] = colorRead;
		DrawIndexedIndirectMerged(
			thisFrameCmdBuffer,
			rndCtx.gfxMergedPipeline,
			colorPassInfo,
			indexBuff,
			VK_INDEX_TYPE_UINT32,
			*rndCtx.cullingCtx.pDrawCmds,
			*rndCtx.cullingCtx.pDrawCount,
			meshletUpperBound,
			&shadingPush,
			sizeof(shadingPush)
		);

		DepthPyramidMultiPass(
			thisFrameCmdBuffer,
			rndCtx.compHiZPipeline,
			depthTarget,
			rndCtx.depthSrv,
			depthPyramid,
			rndCtx.hizSrv,
			rndCtx.hizMipUavs,
			rndCtx.quadMinSamplerIdx,
			vk.globalLayout,
			{32,32,1}
		);

		VkRenderingAttachmentInfo attInfosDbg[] = { colorRead };
		vk_rendering_info colDgbInfo = {
			.viewport = viewport,
			.scissor = scissor,
			.colorAttachments = attInfosDbg,
			.pDepthAttachment = 0
		};
		rndCtx.dbgCtx.DrawCPU( thisFrameCmdBuffer, colDgbInfo, "Draw Occluder-Color", debug_draw_type::TRIANGLE, 
			thisVFrame.pViewData->devicePointer, 1, debugOcclusionWallTransf, cyan );

		if( frameData.freezeMainView )
		{
			VkRenderingAttachmentInfo attInfosDbg[] = { colorRead };
			vk_rendering_info colDgbInfo = {
				.viewport = viewport,
				.scissor = scissor,
				.colorAttachments = attInfosDbg,
				.pDepthAttachment = 0
			};
			rndCtx.dbgCtx.DrawCPU( thisFrameCmdBuffer, colDgbInfo, "Draw Frustum", debug_draw_type::LINE, 
				thisVFrame.pViewData->devicePointer, 1, frameData.frustTransf, yellow );
		}

		if( frameData.dbgDraw )
		{
			//DrawIndirectPass( thisVFrame.cmdBuff,
			//				  gfxDrawIndirDbg,
			//				  &colorRead,
			//				  0,
			//				  drawCmdAabbsBuff,
			//				  drawCountDbgBuff.hndl,
			//				  dbgDrawProgram,
			//				  frameData.activeProjView, scissor );
		}

		thisFrameCmdBuffer.CmdFillVkBuffer( *rndCtx.tonemappingCtx.pLuminanceHistogramBuffer, 0u );
		thisFrameCmdBuffer.CmdFillVkBuffer( *rndCtx.tonemappingCtx.pAtomicWgCounterBuff, 0u );

		VkBufferMemoryBarrier2 zeroInitGlobals[] = {
			VkMakeBufferBarrier2( rndCtx.tonemappingCtx.pLuminanceHistogramBuffer->hndl,
								  VK_ACCESS_2_TRANSFER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
			VkMakeBufferBarrier2( rndCtx.tonemappingCtx.pAtomicWgCounterBuff->hndl,
								VK_ACCESS_2_TRANSFER_WRITE_BIT,
								VK_PIPELINE_STAGE_2_TRANSFER_BIT,
								VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT )
		};
		VkImageMemoryBarrier2 hrdColTargetAcquire[] = { VkMakeImageBarrier2( colorTarget.hndl,
																		 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
																		 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
																		 VK_ACCESS_2_SHADER_READ_BIT,
																		 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
																		 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
																		 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
																		 VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( zeroInitGlobals, hrdColTargetAcquire );
		rndCtx.tonemappingCtx.AverageLuminancePass(
			thisFrameCmdBuffer, frameData.elapsedSeconds, rndCtx.colSrv, colorTargetSize );
		
		imgIdx = vk.pDc->AcquireNextSwapchainImage( thisVFrame.canGetImgSema );
		
		// NOTE: we need exec dependency from acquireImgKHR ( col out + compute shader ) to compute shader
		VkImageMemoryBarrier2 scWriteBarrier[] = { 
			VkMakeImageBarrier2( vk.pDc->sc.imgs[ imgIdx ],
			    0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			    0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT ) 
		};

		VkBufferMemoryBarrier2 avgLumReadBarrier[] = { 
			VkMakeBufferBarrier2( rndCtx.tonemappingCtx.pAverageLuminanceBuffer->hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) 
		};

		thisFrameCmdBuffer.CmdPipelineBarriers( avgLumReadBarrier, scWriteBarrier );


		VK_CHECK( VK_INTERNAL_ERROR( ( colorTarget.width != vk.pDc->sc.width ) || ( colorTarget.height != vk.pDc->sc.height ) ) );
		rndCtx.tonemappingCtx.TonemappingGammaPass( 
			thisFrameCmdBuffer, rndCtx.colSrv, rndCtx.swapchainUavs[ imgIdx ], colorTargetSize );

		VkImageMemoryBarrier2 compositionEndBarriers[] = {
			VkMakeImageBarrier2( colorTarget.hndl,
								 VK_ACCESS_2_SHADER_READ_BIT,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 0, 0,
								 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ),
			VkMakeImageBarrier2( vk.pDc->sc.imgs[ imgIdx ],
								 VK_ACCESS_2_SHADER_WRITE_BIT,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
								 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineImageBarriers( compositionEndBarriers );

		VkViewport uiViewport = { 0, 0, ( float ) vk.pDc->sc.width, ( float ) vk.pDc->sc.height, 0, 1.0f };
		vkCmdSetViewport( thisVFrame.cmdBuff, 0, 1, &uiViewport );

		auto swapchainUIRW = VkMakeAttachemntInfo( 
			vk.pDc->sc.imgViews[ imgIdx ], VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
		rndCtx.imguiCtx.DrawUiPass( thisVFrame.cmdBuff, &swapchainUIRW, 0, scissor, currentFrameIdx );

		VkImageMemoryBarrier2 presentWaitBarrier[] = { 
			VkMakeImageBarrier2( vk.pDc->sc.imgs[ imgIdx ],
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineImageBarriers( presentWaitBarrier );
	}

	gpuData.timeMs = VkCmdReadGpuTimeInMs( thisVFrame.cmdBuff, thisVFrame.gpuTimer );
	thisFrameCmdBuffer.CmdEndCmbBuffer();


	VkSemaphore waitSemas[] = { thisVFrame.canGetImgSema };
	VkCommandBuffer cmdBuffs[] = { thisFrameCmdBuffer.hndl };
	VkSemaphore signalSemas[] = { vk.pDc->sc.canPresentSemas[ imgIdx ], rndCtx.timelineSema };
	u64 signalValues[] = { 0, rndCtx.vFrameIdx }; // NOTE: this is the next frame val
	VkPipelineStageFlags waitDstStageMsk = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;// VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	
	vk.pDc->gfxQueue.QueueSubmit( waitSemas, cmdBuffs, signalSemas, signalValues, waitDstStageMsk );

	vk.pDc->QueuePresent( vk.pDc->gfxQueue, imgIdx );
}

void VkBackendKill()
{
	// NOTE: SHOULDN'T need to check if( VkObj ). Can't create -> app fail
	assert( vk.pDc->device );
	vkDeviceWaitIdle( vk.pDc->device );
	//for( auto& queued : deviceGlobalDeletionQueue ) queued();
	//deviceGlobalDeletionQueue.clear();
	

	vkDestroyDevice( vk.pDc->device, 0 );
#ifdef _VK_DEBUG_
	vkDestroyDebugUtilsMessengerEXT( vk.inst.hndl, vk.inst.dbgMsg, 0 );
#endif
	vkDestroySurfaceKHR( vk.inst.hndl, vk.surf, 0 );
	vkDestroyInstance( vk.inst.hndl, 0 );

	SysDllUnload( vk.inst.dll );
}

#undef HTVK_NO_SAMPLER_REDUCTION
#undef VK_APPEND_DESTROYER
#undef VK_CHECK