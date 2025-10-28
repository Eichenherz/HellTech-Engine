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

// TODO: where to place these ?
extern HINSTANCE hInst;
extern HWND hWnd;


#include "r_data_structs.h"

// TODO:
struct renderer_config
{
	static constexpr u8 MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;

	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	VkFormat		desiredHiZFormat = VK_FORMAT_R32_SFLOAT;
	VkFormat        desiredSwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	u16             renderWidth;
	u16             rednerHeight;
	u8              maxAllowedFramesInFlight = 2;
};
static renderer_config renderCfg = {};

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

	void CmdCopyBuffer( const vk_buffer& src, u64 srcOffset, u64 sizeToCopy, const vk_buffer& dst )
	{
		VkBufferCopy copyRegion = { .srcOffset = srcOffset, .dstOffset = 0, .size = sizeToCopy };
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


#include "asset_compiler.h"


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
	vk_image                    fontsImg;
	VkImageView                 fontsView;
	VkSampler                   fontSampler;

	VkDescriptorSetLayout       descSetLayout;
	VkPipelineLayout            pipelineLayout;
	VkDescriptorUpdateTemplate  descTemplate = {};
	VkPipeline	                pipeline;
	


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

// TODO: buffer resize ?
// TODO: vk formats 
static inline imgui_context ImguiMakeVkContext( vk_device_ctx& dc, VkFormat colDstFormat )
{
	VkSampler fontSampler = VkMakeSampler( 
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


	imgui_context ctx = {};
	ctx.descSetLayout = descSetLayout;
	ctx.pipelineLayout = pipelineLayout;
	ctx.pipeline = pipeline;
	ctx.descTemplate = descTemplate;
	ctx.fontSampler = fontSampler;
	ctx.vtxBuffs[ 0 ] = dc.CreateBuffer( { 
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
		.elemCount = 64 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );
		
	ctx.idxBuffs[ 0 ] = dc.CreateBuffer( { 
		.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		.elemCount = 64 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );
	ctx.vtxBuffs[ 1 ] = dc.CreateBuffer( { 
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
		.elemCount = 64 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );
	ctx.idxBuffs[ 1 ] = dc.CreateBuffer( { 
		.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		.elemCount = 64 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );

	return ctx;
}

__forceinline auto ImguiGetFontImage()
{
	struct retval
	{
		u8* pixels;
		u32 width;
		u32 heigh;
	};

	u8* pixels = 0;
	u32 width = 0, height = 0;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32( &pixels, ( int* ) &width, ( int* ) &height );
	
	return retval{ pixels,width,height };
}

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

static vk_buffer drawCmdBuff;
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
};

inline std::shared_ptr<vk_buffer> StagingManagerGetStagingBuffer( vk_device_ctx& dc, u64 size )
{
	VkBufferUsageFlags usg = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	return std::make_shared<vk_buffer>( dc.CreateBuffer(
		{ .usageFlags = usg, .elemCount = size, .stride = 1, .usage = buffer_usage::STAGING } ) );

}

inline void
StagingManagerPushForRecycle( staging_manager& stgMngr, std::shared_ptr<vk_buffer>& stagingBuff, u64 currentFrameId )
{
	stgMngr.pendingUploads.push_back( { stagingBuff,currentFrameId } );
}

inline static void
StagingManagerUploadBuffer( 
	vk_device_ctx& dc,
	staging_manager& stgMngr, 
	vk_command_buffer& cmdBuff, 
	std::span<const u8> dataIn,
	const vk_buffer& dst,
	u64 currentFrameId 
) {
	auto stagingBuff = StagingManagerGetStagingBuffer( dc, std::size( dataIn ) );
	std::memcpy( stagingBuff->hostVisible, std::data( dataIn ), std::size( dataIn ) );
	StagingManagerPushForRecycle( stgMngr, stagingBuff, currentFrameId );

	cmdBuff.CmdCopyBuffer( *stagingBuff, 0, std::size( dataIn ), dst );
}

static staging_manager stagingManager;

static vk_program   dbgDrawProgram = {};
static VkPipeline   gfxDrawIndirDbg = {};

struct culling_ctx
{
	std::shared_ptr<vk_buffer> pInstanceVisibilityCache;
	std::shared_ptr<vk_buffer> pClusterVisibilityCache;
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
		u32 instVisBuckets = ( instancesUpperBound + 31 ) / 32;
		pInstanceVisibilityCache = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_InstanceVisibilityCache",
			.usageFlags = usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = instVisBuckets,
			.stride = sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } ) );

		u32 mletVisBuckets = ( meshletUpperBound + 31 ) / 32;
		pClusterVisibilityCache = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_ClusterVisibilityCache",
			.usageFlags = usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = mletVisBuckets,
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

		cmdBuff.CmdFillVkBuffer( *pDrawCount, 0u );
		//cmdBuff.CmdFillVkBuffer( drawCountDbgBuff, 0u );
		cmdBuff.CmdFillVkBuffer( *pAtomicWgCounter, 0u );

		VkBufferMemoryBarrier2 beginCullBarriers[] = {
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

			//VkMakeBufferBarrier2( 
			//	drawCountDbgBuff.hndl,
			//	VK_ACCESS_2_TRANSFER_WRITE_BIT,
			//	VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			//	VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
			//	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),

			VkMakeBufferBarrier2( 
				pDispatchIndirect->hndl,
				0,
				0,
				VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),

			//VkMakeBufferBarrier2(
			//	indirectMergedIndexBuff.hndl,
			//	VK_ACCESS_2_INDEX_READ_BIT,
			//	VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
			//	VK_ACCESS_2_SHADER_WRITE_BIT,
			//	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
			VkMakeBufferBarrier2( 
				pAtomicWgCounter->hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		};

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
				u64 instVisCacheAddr;
				u64 atomicWorkgrCounterAddr;
				u64 visInstaceCounterAddr;
				u64 dispatchCmdAddr;
				u32	hizBuffIdx;
				u32	hizSamplerIdx;
				u32 instanceCount;
				u32 camIdx;
				bool latePass;
			} pushConst = {
					.drawCmdsAddr = pDrawCmds->devicePointer,
					.compactedArgsAddr = pCompactedDrawArgs->devicePointer,
					.instVisCacheAddr = pInstanceVisibilityCache->devicePointer,
					.atomicWorkgrCounterAddr = pAtomicWgCounter->devicePointer,
					.visInstaceCounterAddr = pDrawCount->devicePointer,
					.dispatchCmdAddr = pDispatchIndirect->devicePointer,
					.hizBuffIdx = _hizBuffIdx,
					.hizSamplerIdx = samplerIdx,
					.instanceCount = instCount,
					.camIdx = _camIdx,
					.latePass = latePass
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
				VK_ACCESS_2_SHADER_READ_BIT,//VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT ),
			VkMakeBufferBarrier2( 
				drawCmdDbgBuff.hndl,
				VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ),
			//VkMakeBufferBarrier2( 
			//	drawCountDbgBuff.hndl,
			//	VK_ACCESS_2_SHADER_READ_BIT,//VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			//	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//	VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
			//	VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT ),
			VkMakeBufferBarrier2( 
				drawCmdAabbsBuff.hndl,
				VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ),
			VkMakeBufferBarrier2( 
				indirectMergedIndexBuff.hndl,
				VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_INDEX_READ_BIT,
				VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT ),
		};

		cmdBuff.CmdPipelineBufferBarriers( endCullBarriers );
	}
};

// TODO: add the backend here basically 
struct render_context
{
	imgui_context   imguiCtx;
	debug_context   dbgCtx;
	culling_ctx     cullingCtx;

	virtual_frame	vrtFrames[ renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED ];

	std::shared_ptr<vk_image> pColorTarget;
	std::shared_ptr<vk_image> pDepthTarget;

	VkImageView colorView;
	VkImageView depthView;

	u16 colSrv;
	u16 depthSrv;

	// TODO: move to appropriate technique/context
	VkPipeline      gfxZPrepass;
	VkPipeline		gfxPipeline;
	VkPipeline		gfxMeshletPipeline;
	VkPipeline		gfxMergedPipeline;

	VkPipeline		compHiZPipeline;

	VkPipeline		compAvgLumPipe;
	VkPipeline		compTonemapPipe;

	VkSemaphore     timelineSema;
	u64				vFrameIdx = 0;
	u8				framesInFlight;

	// TODO: move to appropriate technique/context
	std::shared_ptr<vk_buffer> pAvgLumBuff;
	std::shared_ptr<vk_buffer> pShaderGlobalScratchpadBuff;
	std::shared_ptr<vk_buffer> pShaderAtomicWorkgoupCounterBuff;

	std::shared_ptr<vk_image> pHiZTarget;

	VkImageView hiZView;
	VkImageView hiZMipViews[ MAX_MIP_LEVELS ];

	VkSampler quadMinSampler;
	VkSampler pbrSampler;

	u16 avgLumIdx;
	u16 shaderGlobalScratchpadIdx;
	u16 shaderAtomicWorkgroupCounterIdx;
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
	vk_swapchain sc;
	vk_instance inst;
	VkSurfaceKHR surf;
	VkPipelineLayout globalLayout;
};

static vk_backend vk;

// TODO: separate from render_context
void VkBackendInit()
{
	vk.inst = VkMakeInstance();

	vk.surf = VkMakeWinSurface( vk.inst.hndl, hInst, hWnd );
	vk.pDc = std::make_unique<vk_device_ctx>( VkMakeDeviceContext( vk.inst.hndl, vk.surf ) );

	vk.sc = VkMakeSwapchain( vk.pDc->device, vk.pDc->gpu, vk.surf, vk.pDc->gfxQueue.index, renderCfg.desiredSwapchainFormat, 3 );

	rndCtx.framesInFlight = renderCfg.maxAllowedFramesInFlight;
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
		vk_shader2 avgLum = VkLoadShader2( "bin/SpirV/compute_AvgLuminanceCsMain.spirv", vk.pDc->device );
		rndCtx.compAvgLumPipe =
			VkMakeComputePipeline( 
				vk.pDc->device, 0, vk.globalLayout, avgLum.shaderModule, 
				{}, vk.pDc->waveSize, avgLum.entryPoint, "Pipeline_Comp_AvgLum" );

		vk_shader2 toneMapper = VkLoadShader2( "bin/SpirV/compute_TonemappingGammaCsMain.spirv", vk.pDc->device );
		rndCtx.compTonemapPipe = VkMakeComputePipeline( 
			vk.pDc->device, 0, vk.globalLayout, toneMapper.shaderModule, {}, 
			vk.pDc->waveSize, toneMapper.entryPoint, "Pipeline_Comp_Tonemapping");
	}
	{
		vk_shader2 downsampler = VkLoadShader2( "bin/SpirV/compute_Pow2DownSamplerCsMain.spirv", vk.pDc->device );
		rndCtx.compHiZPipeline = VkMakeComputePipeline( 
			vk.pDc->device, 0, vk.globalLayout, downsampler.shaderModule, {}, 
			vk.pDc->waveSize, downsampler.entryPoint, "Pipeline_Comp_HiZ" );
	}

	rndCtx.dbgCtx.Init( *vk.pDc, vk.globalLayout, renderCfg );
	rndCtx.dbgCtx.InitData( *vk.pDc, 1 * KB, 1 * KB );


	rndCtx.imguiCtx = ImguiMakeVkContext( *vk.pDc, renderCfg.desiredSwapchainFormat );
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


inline static void
AverageLuminancePass(
	vk_command_buffer	cmdBuff,
	VkPipeline			vkPipeline,
	u16				    hdrColSrcIdx,
    u16				    lumHistoIdx,
    u16				    atomicWorkGrCounterIdx,
    u16				    avgLumIdx,
	const vk_image&     fboHdrColTrg,
	float				dt,
	group_size          grSz
) {
	vk_scoped_label label = cmdBuff.CmdIssueScopedLabel("Averge Lum Pass",{} );
	// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
	avg_luminance_info avgLumInfo = {
		.minLogLum = -10.0f,
		.invLogLumRange = 1.0f / 12.0f,
		.dt = dt
	};
	cmdBuff.CmdBindPipelineAndBindlessDesc( vkPipeline, VK_PIPELINE_BIND_POINT_COMPUTE );
	
	DirectX::XMUINT3 numWorkGrs = { GroupCount( fboHdrColTrg.width, grSz.x ), GroupCount( fboHdrColTrg.height, grSz.y ), 1 };
	struct push_const
	{
		//DirectX::XMUINT3 numWorkGrs;
		avg_luminance_info  avgLumInfo;
		uint				    hdrColSrcIdx;
		uint				    lumHistoIdx;
		uint				    atomicWorkGrCounterIdx;
		uint				    avgLumIdx;

		push_const( avg_luminance_info avgInfo, u16 hdr, u16 lumHIsto, u16 atomicCtr, u16 lum )
			: avgLumInfo( avgInfo ), 
			hdrColSrcIdx( hdr ), lumHistoIdx( lumHIsto ), atomicWorkGrCounterIdx( atomicCtr ), avgLumIdx( lum ) {}
	};
	push_const pushConst{ avgLumInfo, hdrColSrcIdx, lumHistoIdx, atomicWorkGrCounterIdx, avgLumIdx };
	cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );

	cmdBuff.CmdDispatch( numWorkGrs );
}

// TODO: optimize
inline static void
TonemappingGammaPass(
	vk_command_buffer	cmdBuff,
	VkPipeline			tonePipe,
	u16                 hdrColIdx,
	u16                 sdrColIdx,
	u16                 avgLumIdx,
	const vk_image&		fboHdrColTrg,
	group_size          groupSize
) {
	vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Tonemapping Gamma Pass", {} );
	
	cmdBuff.CmdBindPipelineAndBindlessDesc( tonePipe, VK_PIPELINE_BIND_POINT_COMPUTE );
	
	struct push_const
	{
		uint hdrColIdx;
		uint sdrColIdx;
		uint avgLumIdx;

		push_const( uint hdr, uint sdr, uint lum )
			: hdrColIdx(hdr), sdrColIdx(sdr), avgLumIdx(lum){}
	};
	push_const pushConst{ hdrColIdx, sdrColIdx, avgLumIdx };
	cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
	vkCmdDispatch( 
		cmdBuff.hndl, 
		GroupCount( fboHdrColTrg.width, groupSize.x ), 
		GroupCount( fboHdrColTrg.height, groupSize.y ), 1 );
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

void VkInitGlobalResources( vk_device_ctx& dc, render_context& rndCtx, vk_swapchain& sc, vk_descriptor_manager& descManager )
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

		rndCtx.hiZView = VkMakeImgView(
			dc.device, rndCtx.pHiZTarget->hndl, hiZInfo.format, 0, hiZInfo.mipCount, 
			VK_IMAGE_VIEW_TYPE_2D, 0, hiZInfo.layerCount );
		rndCtx.hizSrv = VkAllocDescriptorIdx( 
			descManager, vk_descriptor_info{ rndCtx.hiZView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

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
			.width = sc.width,
			.height = sc.height,
			.layerCount = 1,
			.mipCount = 1,
		};
		rndCtx.pDepthTarget = std::make_shared<vk_image>( dc.CreateImage( info ) );

		rndCtx.depthView = VkMakeImgView(
			dc.device, rndCtx.pDepthTarget->hndl, info.format, 0, info.mipCount, 
			VK_IMAGE_VIEW_TYPE_2D, 0, info.layerCount );

		rndCtx.depthSrv = VkAllocDescriptorIdx( 
			descManager, vk_descriptor_info{ rndCtx.depthView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
	}
	if( rndCtx.pColorTarget == nullptr )
	{
		constexpr VkImageUsageFlags usgFlags =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image_info info = {
			.name = "Img_ColorTarget",
			.format = renderCfg.desiredColorFormat,
			.usg = usgFlags,
			.width = sc.width,
			.height = sc.height,
			.layerCount = 1,
			.mipCount = 1,
		};
		rndCtx.pColorTarget = std::make_shared<vk_image>( dc.CreateImage( info ) );

		rndCtx.colorView = VkMakeImgView(
			dc.device, rndCtx.pColorTarget->hndl, info.format, 0, info.mipCount, 
			VK_IMAGE_VIEW_TYPE_2D, 0, info.layerCount );

		rndCtx.colSrv = VkAllocDescriptorIdx( 
			descManager, vk_descriptor_info{ rndCtx.colorView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
	
		rndCtx.pbrSampler = 
			VkMakeSampler( dc.device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );

		rndCtx.pbrSamplerIdx = VkAllocDescriptorIdx( descManager, vk_descriptor_info{ rndCtx.pbrSampler } );
	}

	for( u64 scImgIdx = 0; scImgIdx < sc.imgCount; ++scImgIdx )
	{
		rndCtx.swapchainUavs[ scImgIdx ] = VkAllocDescriptorIdx( 
			descManager, vk_descriptor_info{ sc.imgViews[ scImgIdx ], VK_IMAGE_LAYOUT_GENERAL } );
	}

	rndCtx.pAvgLumBuff = std::make_shared<vk_buffer>( dc.CreateBuffer( {
		.name = "Buff_AvgLum",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.elemCount = 1,
		.stride = sizeof( float ),
		.usage = buffer_usage::GPU_ONLY } ) );
	rndCtx.avgLumIdx = VkAllocDescriptorIdx( descManager, vk_descriptor_info{ *rndCtx.pAvgLumBuff } );

	rndCtx.pShaderAtomicWorkgoupCounterBuff = std::make_unique<vk_buffer>( dc.CreateBuffer( {
		.name = "Buff_ShaderAtomicWgCounter",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } ) );
	rndCtx.shaderAtomicWorkgroupCounterIdx = VkAllocDescriptorIdx(
		descManager, vk_descriptor_info{ *rndCtx.pShaderAtomicWorkgoupCounterBuff } );

	rndCtx.pShaderGlobalScratchpadBuff = std::make_unique<vk_buffer>( dc.CreateBuffer( {
		.name = "Buff_ShaderGlobals",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.elemCount = 4,
		.stride = sizeof( u64 ),
		.usage = buffer_usage::GPU_ONLY } ) ); 
	rndCtx.shaderGlobalScratchpadIdx = VkAllocDescriptorIdx(
		descManager, vk_descriptor_info{ *rndCtx.pShaderGlobalScratchpadBuff } );
}

static std::vector<vk_image> textures;
static std::vector<VkImageView> textureViews;

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

	const std::span<material_data> mtrlDesc = {
		( material_data* ) ( std::data( binaryData ) + fileFooter.mtrlsByteRange.offset ),
		fileFooter.mtrlsByteRange.size / sizeof( material_data ) };

	const std::span<image_metadata> imgDesc = {
		( image_metadata* ) ( std::data( binaryData ) + fileFooter.imgsByteRange.offset ),
		fileFooter.imgsByteRange.size / sizeof( image_metadata ) };


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
		const std::span<u8> vtxView = { std::data( binaryData ) + fileFooter.vtxByteRange.offset, fileFooter.vtxByteRange.size };

		globVertexBuff = dc.CreateBuffer( {
			.name = "Buff_Vtx",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) std::size( vtxView ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );

		StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, vtxView, globVertexBuff, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2( 
			globVertexBuff.hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ) );
	}
	{
		const std::span<u8> idxSpan = { std::data( binaryData ) + fileFooter.idxByteRange.offset, fileFooter.idxByteRange.size };

		indexBuff = dc.CreateBuffer( {
			.name = "Buff_Idx",
			.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) std::size( idxSpan ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );

		StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, idxSpan, indexBuff, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2( 
			indexBuff.hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_INDEX_READ_BIT, 
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT ) );
	}
	{
		meshBuff = dc.CreateBuffer( {
			.name = "Buff_MeshDesc",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( BYTE_COUNT( meshes ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } ); 

		StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, CastSpanAsU8ReadOnly( meshes ), meshBuff, currentFrameId );	 

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ) );
	}
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
	{
		const std::span<meshlet> mletView = { ( meshlet* ) std::data( binaryData ) + fileFooter.mletsByteRange.offset,
			fileFooter.mletsByteRange.size / sizeof( meshlet ) };

		meshletBuff = dc.CreateBuffer( {
			.name = "Buff_Meshlets",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( std::size( mletView ) ),
			.stride = sizeof( meshlet ),
			.usage = buffer_usage::GPU_ONLY } );  

		StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, CastSpanAsU8ReadOnly( mletView ), meshletBuff, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) );
	}
	{
		const std::span<u8> mletDataView = { 
			std::data( binaryData ) + fileFooter.mletsDataByteRange.offset,
			fileFooter.mletsDataByteRange.size };

		meshletDataBuff = dc.CreateBuffer( {
			.name = "Buff_MeshletData",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												   VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT ,
			.elemCount = (u32) ( std::size( mletDataView ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );  

		StagingManagerUploadBuffer( dc, stagingManager, cmdBuff, mletDataView, meshletDataBuff, currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletDataBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) );
	}


	drawCmdBuff = dc.CreateBuffer( {
		.name = "Buff_IndirectDrawCmds",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = std::size( instDesc ),
		.stride = sizeof( draw_command ),
		.usage = buffer_usage::GPU_ONLY } );   

	drawCmdDbgBuff = dc.CreateBuffer( {
		.name = "Buff_IndirectDrawCmds",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = std::size( instDesc ),
		.stride = sizeof( draw_command ),
		.usage = buffer_usage::GPU_ONLY } );  

	// TODO: expose from asset compiler 
	constexpr u64 MAX_TRIS = 256;
	//u64 maxByteCountMergedIndexBuff = std::size( instDesc ) * ( meshletBuff.size / sizeof( meshlet ) ) * MAX_TRIS * 3ull;
	u64 maxByteCountMergedIndexBuff = 10 * MB;

	intermediateIndexBuff = dc.CreateBuffer( {
		.name = "Buff_IntermediateIdx",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = maxByteCountMergedIndexBuff,
		.stride = 1,
		.usage = buffer_usage::GPU_ONLY } );

	indirectMergedIndexBuff = dc.CreateBuffer( {
		.name = "Buff_MergedIdx",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = maxByteCountMergedIndexBuff,
		.stride = 1,
		.usage = buffer_usage::GPU_ONLY } ); 

	drawCmdAabbsBuff = dc.CreateBuffer( {
		.name = "Buff_AabbsDrawCms",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 10'000,
		.stride = sizeof( draw_indirect ),
		.usage = buffer_usage::GPU_ONLY } );  

	// NOTE: create and texture uploads
	std::vector<VkImageMemoryBarrier2> imageBarriers;
	{
		imageBarriers.reserve( std::size( imgDesc ) );

		u64 newTexturesOffset = std::size( textures );

		for( const image_metadata& meta : imgDesc )
		{
			image_info info = GetImageInfoFromMetadata( meta, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
			vk_image img = dc.CreateImage( info );
			textures.push_back( img );

			imageBarriers.push_back( VkMakeImageBarrier2(
				img.hndl,
				0, 0,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );
		}

		cmdBuff.CmdPipelineImageBarriers( imageBarriers );

		imageBarriers.resize( 0 );

		assert( u32( -1 ) >= fileFooter.texBinByteRange.size );

		const u8* pTexBinData = std::data( binaryData ) + fileFooter.texBinByteRange.offset;
		auto stagingBuff = StagingManagerGetStagingBuffer( dc, fileFooter.texBinByteRange.size );
		std::memcpy( stagingBuff->hostVisible, pTexBinData, stagingBuff->sizeInBytes );
		StagingManagerPushForRecycle( stagingManager, stagingBuff, currentFrameId );

		for( u64 i = 0; i < std::size( imgDesc ); ++i )
		{
			const vk_image& dst = textures[ i + newTexturesOffset ];
			cmdBuff.CmdCopyBufferToSingleImageSubresource( *stagingBuff, imgDesc[ i ].texBinRange.offset, dst );

			imageBarriers.push_back( VkMakeImageBarrier2(
				dst.hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );
		}

		auto CreateImgViewAndAllocDscIdx = [&] ( const vk_image& tex ) -> u16
			{
				VkImageView imgView = VkMakeImgView(
					dc.device, tex.hndl, tex.format, 0, tex.mipCount, VK_IMAGE_VIEW_TYPE_2D, 0, tex.layerCount );
				textureViews.push_back( imgView );

				return VkAllocDescriptorIdx( vk.descManager, vk_descriptor_info{ imgView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
			};

		textureViews.reserve( std::size( textures ) );

		// NOTE: we assume materials have an abs idx for the textures
		std::vector<material_data> mtrls = {};
		for( const material_data& m : mtrlDesc )
		{
			mtrls.push_back( m );
			material_data& refM = mtrls[ std::size( mtrls ) - 1 ];

			const auto& mBaseCol = textures[ m.baseColIdx ];
			const auto& mNormalMap = textures[ m.normalMapIdx ];
			const auto& mOccRoughMetal = textures[ m.occRoughMetalIdx ];

			refM.baseColIdx = CreateImgViewAndAllocDscIdx( mBaseCol );
			refM.normalMapIdx = CreateImgViewAndAllocDscIdx( mNormalMap );
			refM.occRoughMetalIdx = CreateImgViewAndAllocDscIdx( mOccRoughMetal );
		}
		{
			VkBufferUsageFlags usg =
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

			materialsBuff = dc.CreateBuffer( {
				.name = "Buff_Mtrls",
				.usageFlags = usg,
				.elemCount = ( u32 ) std::size( mtrls ),
				.stride = sizeof( decltype( mtrls )::value_type ),
				.usage = buffer_usage::GPU_ONLY
													 } );

			StagingManagerUploadBuffer(
				dc, stagingManager, cmdBuff, 
				CastSpanAsU8ReadOnly( std::span<material_data>{mtrls} ), materialsBuff, currentFrameId );

			buffBarriers.push_back( VkMakeBufferBarrier2(
				materialsBuff.hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ) );
		}
	}
	cmdBuff.CmdPipelineBarriers( buffBarriers, imageBarriers );
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
		buffer_info info = {
			.name = "Buff_VirtualFrame_ViewBuff",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.elemCount = std::size( frameData.views ),
			.stride = sizeof( view_data ),
			.usage = buffer_usage::HOST_VISIBLE
		};
		thisVFrame.pViewData = std::make_shared<vk_buffer>( vk.pDc->CreateBuffer( info ) );
		thisVFrame.viewDataIdx = VkAllocDescriptorIdx( vk.descManager, vk_descriptor_info{ *thisVFrame.pViewData } );
	}
	assert( thisVFrame.pViewData->sizeInBytes == BYTE_COUNT( frameData.views ) );
	std::memcpy( thisVFrame.pViewData->hostVisible, std::data( frameData.views ), BYTE_COUNT( frameData.views ) );

	// TODO: remove from here
	static bool rscCpy = false;
	if( !rscCpy )
	{
		rndCtx.dbgCtx.UploadDebugGeometry();
		rscCpy = true;
	}
	
	vk_command_buffer thisFrameCmdBuffer = { thisVFrame.cmdBuff, vk.globalLayout, vk.descManager.set };

	std::vector<vk_descriptor_write> vkDescUpdateCache;
	static bool initResources = false;
	if( !initResources )
	{
		VkInitGlobalResources( *vk.pDc, rndCtx, vk.sc, vk.descManager );

		if( !rndCtx.imguiCtx.fontsImg.hndl )
		{
			auto [pixels, width, height] = ImguiGetFontImage();

			constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
			constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			rndCtx.imguiCtx.fontsImg = vk.pDc->CreateImage( {
				.name = "Img_ImGuiFonts",
				.format = format,
				.usg = usgFlags,
				.width = (u16)width,
				.height = (u16)height,
				.layerCount = 1,
				.mipCount = 1,
			});

			rndCtx.imguiCtx.fontsView = VkMakeImgView(
				vk.pDc->device, rndCtx.imguiCtx.fontsImg.hndl, rndCtx.imguiCtx.fontsImg.format, 0, 
				rndCtx.imguiCtx.fontsImg.mipCount, VK_IMAGE_VIEW_TYPE_2D, 0,  rndCtx.imguiCtx.fontsImg.layerCount );
		}

		VkUploadResources( *vk.pDc, stagingManager, thisFrameCmdBuffer, entities, currentFrameIdx );

		u32 instCount = instDescBuff.sizeInBytes / sizeof( instance_desc );
		u32 mletCount = meshletBuff.sizeInBytes / sizeof( meshlet );
		rndCtx.cullingCtx.InitSceneDependentData( *vk.pDc, instCount, mletCount * instCount );

		thisFrameCmdBuffer.CmdFillVkBuffer( *rndCtx.cullingCtx.pInstanceVisibilityCache, 0u );
		thisFrameCmdBuffer.CmdFillVkBuffer( *rndCtx.pAvgLumBuff, 0u );

		VkBufferMemoryBarrier2 initBuffersBarriers[] = {
			VkMakeBufferBarrier2( rndCtx.pAvgLumBuff->hndl,
								  VK_ACCESS_2_TRANSFER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
			VkMakeBufferBarrier2( rndCtx.cullingCtx.pInstanceVisibilityCache->hndl,
								  VK_ACCESS_2_TRANSFER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		};

		VkImageMemoryBarrier2 initBarriers[] = {
			VkMakeImageBarrier2( 
				rndCtx.imguiCtx.fontsImg.hndl, 0, 0,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT )
		};

		thisFrameCmdBuffer.CmdPipelineBarriers( initBuffersBarriers, initBarriers );
		initResources = true;
	}
	

	static bool rescUploaded = 0;
	if( !rescUploaded )
	{
		auto [pixels, width, height] = ImguiGetFontImage();

		u64 uploadSize = width * height * sizeof( u32 );
		buffer_info buffInfo = {
			.name = "Buff_ImGuiTexStaging",
			.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
			.elemCount = uploadSize,
			.stride = 1,
			.usage = buffer_usage::STAGING
		};
		auto upload = std::make_shared<vk_buffer>( vk.pDc->CreateBuffer( buffInfo ) );
		std::memcpy( upload->hostVisible, pixels, uploadSize );
		StagingManagerPushForRecycle( stagingManager, upload, currentFrameIdx );

		thisFrameCmdBuffer.CmdCopyBufferToSingleImageSubresource( *upload, 0, rndCtx.imguiCtx.fontsImg );

		VkImageMemoryBarrier2 fontsBarrier[] = { VkMakeImageBarrier2( rndCtx.imguiCtx.fontsImg.hndl,
												 VK_ACCESS_2_TRANSFER_WRITE_BIT,
												 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
												 VK_ACCESS_2_SHADER_READ_BIT,
												 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
												 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
												 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
												 VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( {}, fontsBarrier );

		rescUploaded = 1;
	}

	VkDescriptorManagerFlushUpdates( vk.descManager, vk.pDc->device );

	const vk_image& depthTarget = *rndCtx.pDepthTarget;
	const vk_image& depthPyramid = *rndCtx.pHiZTarget;
	const vk_image& colorTarget = *rndCtx.pColorTarget;

	auto depthWrite = VkMakeAttachemntInfo( rndCtx.depthView, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto depthRead = VkMakeAttachemntInfo( rndCtx.depthView, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorWrite = VkMakeAttachemntInfo( rndCtx.colorView, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorRead = VkMakeAttachemntInfo( rndCtx.colorView, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );

	VkViewport viewport = VkGetSwapchainViewport( vk.sc );
	VkRect2D scissor = VkGetSwapchianScissor( vk.sc );

	u32 instCount = instDescBuff.sizeInBytes / sizeof( instance_desc );
	u32 mletCount = meshletBuff.sizeInBytes / sizeof( meshlet );
	u32 meshletUpperBound = instCount * mletCount;

	DirectX::XMMATRIX t = DirectX::XMMatrixMultiply( 
		DirectX::XMMatrixScaling( 180.0f, 100.0f, 60.0f ), DirectX::XMMatrixTranslation( 20.0f, -10.0f, -60.0f ) );
	DirectX::XMFLOAT4X4A debugOcclusionWallTransf;
	DirectX::XMStoreFloat4x4A( &debugOcclusionWallTransf, t );

	VkResetGpuTimer( thisVFrame.cmdBuff, thisVFrame.gpuTimer );
	u32 imgIdx;
	{
		vk_time_section timePipeline = { thisVFrame.cmdBuff, thisVFrame.gpuTimer.queryPool, 0 };

		rndCtx.cullingCtx.Execute( 
			thisFrameCmdBuffer, 
			depthPyramid, 
			instCount,
			thisVFrame.viewDataIdx,
			rndCtx.hizSrv,
			rndCtx.quadMinSamplerIdx,
			false
		);

		VkImageMemoryBarrier2 acquireDepthBarriers[] = {
			VkMakeImageBarrier2(
				depthTarget.hndl,
				0, 0,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_IMAGE_ASPECT_DEPTH_BIT ),
		};
		thisFrameCmdBuffer.CmdPipelineBarriers( {}, acquireDepthBarriers );

		struct { u64 vtxAddr, transfAddr, mletDataAddr, compactedArgsAddr; u32 camIdx; } zPrepassPush = {
			globVertexBuff.devicePointer, 
			instDescBuff.devicePointer, 
			meshletDataBuff.devicePointer,
			rndCtx.cullingCtx.pCompactedDrawArgs->devicePointer,
			thisVFrame.viewDataIdx 
		};

		vk_rendering_info zPrepassInfo = {
			.viewport = viewport,
			.scissor = scissor,
			.colorAttachments = {},
			.pDepthAttachment = &depthWrite
		};

		DrawIndexedIndirectMerged(
			thisFrameCmdBuffer,
			rndCtx.gfxZPrepass,
			zPrepassInfo,
			indexBuff,
			VK_INDEX_TYPE_UINT32,
			*rndCtx.cullingCtx.pDrawCmds,
			*rndCtx.cullingCtx.pDrawCount,
			meshletUpperBound,
			&zPrepassPush,
			sizeof(zPrepassPush)
		);

		VkImageMemoryBarrier2 acquireColBarriers[] = {
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
		thisFrameCmdBuffer.CmdPipelineImageBarriers( acquireColBarriers );

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

		VkRenderingAttachmentInfo attInfosDbg[] = { colorRead };
		vk_rendering_info colDgbInfo = {
			.viewport = viewport,
			.scissor = scissor,
			.colorAttachments = attInfosDbg,
			.pDepthAttachment = 0
		};
		rndCtx.dbgCtx.DrawCPU( thisFrameCmdBuffer, colDgbInfo, "Draw Occluder-Color", debug_draw_type::TRIANGLE, 
			thisVFrame.pViewData->devicePointer, 1, debugOcclusionWallTransf, magenta );

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

		thisFrameCmdBuffer.CmdFillVkBuffer( *rndCtx.pShaderGlobalScratchpadBuff, 0u );
		thisFrameCmdBuffer.CmdFillVkBuffer( *rndCtx.pShaderAtomicWorkgoupCounterBuff, 0u );

		VkBufferMemoryBarrier2 zeroInitGlobals[] = {
			VkMakeBufferBarrier2( rndCtx.pShaderGlobalScratchpadBuff->hndl,
								  VK_ACCESS_2_TRANSFER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
			VkMakeBufferBarrier2( rndCtx.pShaderAtomicWorkgoupCounterBuff->hndl,
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

		AverageLuminancePass(
			thisFrameCmdBuffer,
			rndCtx.compAvgLumPipe,
			rndCtx.colSrv,
			rndCtx.shaderGlobalScratchpadIdx,
			rndCtx.shaderAtomicWorkgroupCounterIdx,
			rndCtx.avgLumIdx,
			colorTarget,
			frameData.elapsedSeconds, { 16, 16, 1 } );

		
		VK_CHECK( vkAcquireNextImageKHR( vk.pDc->device, vk.sc.swapchain, UINT64_MAX, thisVFrame.canGetImgSema, 0, &imgIdx ) );

		// NOTE: we need exec dependency from acquireImgKHR ( col out + compute shader ) to compute shader
		VkImageMemoryBarrier2 scWriteBarrier[] =
		{ VkMakeImageBarrier2( vk.sc.imgs[ imgIdx ],
								 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 0,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 VK_IMAGE_LAYOUT_UNDEFINED,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ) };

		VkBufferMemoryBarrier2 avgLumReadBarrier[] =
		{ VkMakeBufferBarrier2( rndCtx.pAvgLumBuff->hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( avgLumReadBarrier, scWriteBarrier );


		VK_CHECK( VK_INTERNAL_ERROR( ( colorTarget.width != vk.sc.width ) || ( colorTarget.height != vk.sc.height ) ) );
		TonemappingGammaPass( thisFrameCmdBuffer,
							  rndCtx.compTonemapPipe,
							  rndCtx.colSrv,
							  rndCtx.swapchainUavs[ imgIdx ],
							  rndCtx.avgLumIdx,
							  colorTarget,
							  { 16,16,1 } );

		VkImageMemoryBarrier2 compositionEndBarriers[] = {
			VkMakeImageBarrier2( colorTarget.hndl,
								 VK_ACCESS_2_SHADER_READ_BIT,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 0, 0,
								 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ),
			VkMakeImageBarrier2( vk.sc.imgs[ imgIdx ],
								 VK_ACCESS_2_SHADER_WRITE_BIT,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
								 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineImageBarriers( compositionEndBarriers );

		VkViewport uiViewport = { 0, 0, ( float ) vk.sc.width, ( float ) vk.sc.height, 0, 1.0f };
		vkCmdSetViewport( thisVFrame.cmdBuff, 0, 1, &uiViewport );

		auto swapchainUIRW = VkMakeAttachemntInfo( 
			vk.sc.imgViews[ imgIdx ], VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
		rndCtx.imguiCtx.DrawUiPass( thisVFrame.cmdBuff, &swapchainUIRW, 0, scissor, currentFrameIdx );


		VkImageMemoryBarrier2 presentWaitBarrier[] = { VkMakeImageBarrier2(
			vk.sc.imgs[ imgIdx ],
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineImageBarriers( presentWaitBarrier );
	}

	gpuData.timeMs = VkCmdReadGpuTimeInMs( thisVFrame.cmdBuff, thisVFrame.gpuTimer );
	thisFrameCmdBuffer.CmdEndCmbBuffer();


	VkSemaphore waitSemas[] = { thisVFrame.canGetImgSema };
	VkCommandBuffer cmdBuffs[] = { thisFrameCmdBuffer.hndl };
	VkSemaphore signalSemas[] = { vk.sc.canPresentSemas[ imgIdx ], rndCtx.timelineSema };
	u64 signalValues[] = { 0, rndCtx.vFrameIdx }; // NOTE: this is the next frame val
	VkPipelineStageFlags waitDstStageMsk = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;// VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	
	VkQueueSubmit( &vk.pDc->gfxQueue, waitSemas, cmdBuffs, signalSemas, signalValues, waitDstStageMsk );


	VkPresentInfoKHR presentInfo = { 
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vk.sc.canPresentSemas[ imgIdx ],
		.swapchainCount = 1,
		.pSwapchains = &vk.sc.swapchain,
		.pImageIndices = &imgIdx
	};
	VK_CHECK( vkQueuePresentKHR( vk.pDc->gfxQueue.hndl, &presentInfo ) );
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