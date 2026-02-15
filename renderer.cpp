#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES

#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>

#include <Volk/volk.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string_view>
#include <charconv>
#include <span>
#include <format>
#include <memory>

#include <EASTL/bonus/ring_buffer.h>

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




#include "vk_context.h"

#include "vk_pso.h"

struct frame_resources
{
	//vk_gpu_timer gpuTimer;

	std::shared_ptr<vk_buffer>		pViewData;

	desc_handle viewDataIdx;
};

#include "r_data_structs.h"

// NOTE: clear depth to 0 bc we use RevZ
constexpr VkClearValue DEPTH_CLEAR_VAL = {};
constexpr VkClearValue RT_CLEAR_VAL = {};

enum render_target_op : u32
{
	LOAD = VK_ATTACHMENT_LOAD_OP_LOAD,
	LOAD_CLEAR = VK_ATTACHMENT_LOAD_OP_CLEAR,
	STORE = VK_ATTACHMENT_STORE_OP_STORE
};

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



#include "asset_compiler.h"


inline VkFormat VkGetFormat( texture_format t )
{
	switch( t )
	{
	case TEXTURE_FORMAT_RBGA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
	case TEXTURE_FORMAT_RBGA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
		//case TEXTURE_FORMAT_BC1_RGB_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
		//case TEXTURE_FORMAT_BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
	case TEXTURE_FORMAT_UNDEFINED: HT_ASSERT( 0 );
	}
}
inline VkImageType VkGetImageType( texture_type t )
{
	switch( t )
	{
	case TEXTURE_TYPE_2D: return VK_IMAGE_TYPE_2D;
	case TEXTURE_TYPE_3D: return VK_IMAGE_TYPE_3D;
	default: HT_ASSERT( 0 ); return VK_IMAGE_TYPE_MAX_ENUM;
	}
}

// TODO: ensure mipmapMode in assetcmpl
// TODO: addrModeW ?
// TODO: more stuff ?
//inline VkSamplerCreateInfo VkMakeSamplerInfo( sampler_config config )
//{
//	HT_ASSERT( 0 );
//	VkSamplerCreateInfo vkSamplerInfo = { 
//		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
//		.magFilter = VkGetFilterTypeFromGltf( config.mag ),
//		.minFilter = VkGetFilterTypeFromGltf( config.min ),
//		.mipmapMode =  VkGetMipmapTypeFromGltf( config.min ),
//		.addressModeU = VkGetAddressModeFromGltf( config.addrU ),
//		.addressModeV = VkGetAddressModeFromGltf( config.addrV )
//	};
//
//	return vkSamplerInfo;
//}

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

	void Init( vk_context& dc )
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

		unique_shader_ptr vtx = dc.CreateShaderFromSpirv( 
			SysReadFile( "bin/SpirV/vertex_ImGuiVsMain.spirv" ) );
		unique_shader_ptr frag = dc.CreateShaderFromSpirv( 
			SysReadFile( "bin/SpirV/pixel_ImGuiPsMain.spirv" ) );

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

		vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		VkPipeline pipeline = dc.CreateGfxPipeline(
			shaderStages, dynamicStates, &renderCfg.desiredSwapchainFormat, 1, VK_FORMAT_UNDEFINED, guiState );
	}

	void InitResources( vk_context& dc, VkFormat colDstFormat )
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

		HT_ASSERT( guiDrawData->TotalVtxCount < u16( -1 ) );
		HT_ASSERT( guiDrawData->TotalVtxCount * sizeof( ImDrawVert ) < vtxBuff.sizeInBytes );

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

constexpr const char* ToString( debug_draw_type t )
{
	switch( t )
	{
	case debug_draw_type::LINE:     return "LINE";
	case debug_draw_type::TRIANGLE: return "TRIANGLE";
	}
	return "UNKNOWN";
}

// TODO: use instancing for drawing ?
// TODO: double buffer debug geometry ?
struct debug_context
{
	std::shared_ptr<vk_buffer> pLinesBuff;
	std::shared_ptr<vk_buffer> pTrisBuff;
	std::shared_ptr<vk_buffer> pDrawCount;

	VkPipeline	drawAsLines;
	VkPipeline	drawAsTriangles;

	void Init( vk_context& dc, VkPipelineLayout globalLayout, vk_renderer_config& rndCfg )
	{
		unique_shader_ptr vtx = dc.CreateShaderFromSpirv( 
			SysReadFile( "Shaders/v_cpu_dbg_draw.vert.spv" ) );
		unique_shader_ptr frag = dc.CreateShaderFromSpirv( 
			SysReadFile( "Shaders/f_pass_col.frag.spv" ) );

		static_assert( worldLeftHanded );

		{
			vk_gfx_pipeline_state lineDrawPipelineState = {
				.polyMode = VK_POLYGON_MODE_LINE,
				.cullFlags = VK_CULL_MODE_NONE,
				.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
				.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
				.depthWrite = VK_FALSE,
				.depthTestEnable = VK_FALSE,
				.blendCol = VK_FALSE,
			};
			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

			vk_gfx_shader_stage shaders[] = { *vtx, *frag };
			drawAsLines = dc.CreateGfxPipeline(  
				shaders, dynamicStates, &rndCfg.desiredColorFormat, 1, VK_FORMAT_UNDEFINED, lineDrawPipelineState );
		}
		
		{
			vk_gfx_pipeline_state triDrawPipelineState = {
				.polyMode = VK_POLYGON_MODE_FILL,
				.cullFlags = VK_CULL_MODE_NONE,
				.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
				.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				.depthWrite = VK_TRUE,
				.depthTestEnable = VK_TRUE,
				.blendCol = VK_TRUE
			};

			vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

			drawAsTriangles = dc.CreateGfxPipeline( 
				shaderStages, dynamicStates, &rndCfg.desiredColorFormat, 1, rndCfg.desiredDepthFormat, triDrawPipelineState );
		}

		pDrawCount = std::make_shared<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_DbgDrawCount",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
																   .elemCount = 1,
																   .stride = sizeof( u32 ),
																   .usage = buffer_usage::GPU_ONLY } ) );  
	}

	void InitData( vk_context& dc, u32 dbgLinesSizeInBytes, u32 dbgTrianglesSizeInBytes )
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

		HT_ASSERT( pLinesBuff->sizeInBytes >= BYTE_COUNT( lineVtxBuff ) );
		HT_ASSERT( pTrisBuff->sizeInBytes >= BYTE_COUNT( trisVtxBuff ) );
		std::memcpy( pLinesBuff->hostVisible, std::data( lineVtxBuff ), BYTE_COUNT( lineVtxBuff ) );
		std::memcpy( pTrisBuff->hostVisible, std::data( trisVtxBuff ), BYTE_COUNT( trisVtxBuff ) );
	}

	// TODO: multi draw with params
	void DrawCPU( 
		vk_command_buffer&        cmdBuff, 
		const vk_rendering_info&  renderingInfo, 
		debug_draw_type           ddType, 
		u64                       viewAddr, 
		u32                       viewIdx,
		const mat4&      transf,
		u32 color
	) {
		std::array<char, 64> fixedStr = {};
		std::format_to_n( 
			&fixedStr[ 0 ], std::size( fixedStr ) - 1 /* for \0 */, "debug_context.DrawCPU: {}", ToString( ddType ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( &fixedStr[ 0 ], {} );
		auto dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

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


static vk_buffer globVertexBuff;
static vk_buffer indexBuff;
static vk_buffer meshBuff;

static vk_buffer meshletBuff;
static vk_buffer meshletDataBuff;

static vk_buffer materialsBuff;
static vk_buffer instDescBuff;
static vk_buffer lightsBuff;

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
		vk_context& dc, 
		std::span<const T> dataIn,
		u64 currentFrameId
	) {
		u64 sizeInBytes = std::size( dataIn ) * sizeof( T );
		auto stagingBuff = std::make_shared<vk_buffer>( dc.CreateBuffer( { 
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
			.elemCount = sizeInBytes, 
			.stride = 1, 
			.usage = buffer_usage::STAGING } ) );
		std::memcpy( stagingBuff->hostVisible, std::data( dataIn ), sizeInBytes );

		PushForRecycle( stagingBuff, currentFrameId );

		return stagingBuff;
	}
};




inline static void
StagingManagerUploadBuffer( 
	vk_context& dc,
	staging_manager& stgMngr, 
	vk_command_buffer& cmdBuff, 
	std::span<const u8> dataIn,
	const vk_buffer& dst,
	u64 currentFrameId 
) {

	//tagingManagerPushForRecycle( stgMngr, stagingBuff, currentFrameId );


}

static staging_manager stagingManager;

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
	VkPipeline      compClusterCullPipe;

	void Init( vk_context& dc )
	{
		unique_shader_ptr drawCull = dc.CreateShaderFromSpirv( SysReadFile( "Shaders/c_draw_cull.comp.spv" ) );
		compPipeline = dc.CreateComptuePipeline( *drawCull, { dc.waveSize }, "Pipeline_Comp_DrawCull" );

		//vk_shader clusterCull = dc.CreateShaderFromSpirv( "Shaders/c_meshlet_cull.comp.spv", dc.device );
		//compClusterCullPipe = VkMakeComputePipeline( 
		//	dc.device, 0, pipelineLayout, clusterCull.module, {}, dc.waveSize, SHADER_ENTRY_POINT, "Pipeline_Comp_ClusterCull" );

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

	void InitSceneDependentData( vk_context& dc, u32 instancesUpperBound, u32 meshletUpperBound )
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
			//cmdBuff.CmdFillVkBuffer( *pInstanceOccludedCache, 0u );
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
				u64 visInstsAddr = 0;//intermediateIndexBuff.devicePointer;
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

	desc_handle avgLumIdx;
	desc_handle atomicWgCounterIdx;
	desc_handle lumHistoIdx;

	void Init( vk_context& dc )
	{
		unique_shader_ptr avgLum = dc.CreateShaderFromSpirv( 
			SysReadFile( "bin/SpirV/compute_AvgLuminanceCsMain.spirv" ) );
		compAvgLumPipe = dc.CreateComptuePipeline( *avgLum, {}, "Pipeline_Comp_AvgLum" );

		unique_shader_ptr toneMapper = dc.CreateShaderFromSpirv( 
			SysReadFile( "bin/SpirV/compute_TonemappingGammaCsMain.spirv" ) );
		compTonemapPipe = dc.CreateComptuePipeline( *toneMapper, {}, "Pipeline_Comp_Tonemapping" );

		VkBufferUsageFlags usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		pAverageLuminanceBuffer = std::make_shared<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_AvgLum",
			.usageFlags = usageFlags,
			.elemCount = 1,
			.stride = sizeof( float ),
			.usage = buffer_usage::GPU_ONLY } ) );
		avgLumIdx = dc.AllocDescriptor( vk_descriptor_info{ *pAverageLuminanceBuffer } );

		pAtomicWgCounterBuff = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_TonemappingAtomicWgCounter",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = 1,
			.stride = sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } ) );
		atomicWgCounterIdx = dc.AllocDescriptor( vk_descriptor_info{ *pAtomicWgCounterBuff } );

		pLuminanceHistogramBuffer = std::make_unique<vk_buffer>( dc.CreateBuffer( {
			.name = "Buff_LumHisto",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = 4,
			.stride = sizeof( u64 ),
			.usage = buffer_usage::GPU_ONLY } ) ); 
		lumHistoIdx = dc.AllocDescriptor( vk_descriptor_info{ *pLuminanceHistogramBuffer } );
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
		} pushConst = { avgLumInfo, hdrColSrcIdx, lumHistoIdx.slot, atomicWgCounterIdx.slot, avgLumIdx.slot };
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
		} pushConst = { hdrColIdx, sdrColIdx, avgLumIdx.slot };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		cmdBuff.CmdDispatch( numWorkGrs );
	}
};

struct depth_pyramid_pass
{
	VkPipeline		pipeline;
	std::shared_ptr<vk_image> pHiZTarget;
	VkImageView hiZMipViews[ MAX_MIP_LEVELS ];

	VkSampler quadMinSampler;

	desc_handle hizSrv;

	desc_handle hizMipUavs[ MAX_MIP_LEVELS ];
	desc_handle quadMinSamplerIdx;

	void Init( vk_context& vkCtx )
	{
		unique_shader_ptr downsampler = vkCtx.CreateShaderFromSpirv( 
			SysReadFile( "bin/SpirV/compute_Pow2DownSamplerCsMain.spirv" ) );
		pipeline = vkCtx.CreateComptuePipeline( *downsampler, {}, "Pipeline_Comp_HiZ" );

		u16 squareDim = 512;
		u8 hiZMipCount = GetImgMipCountForPow2( squareDim, squareDim, MAX_MIP_LEVELS );

		HT_ASSERT( MAX_MIP_LEVELS >= hiZMipCount );

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

		pHiZTarget = std::make_shared<vk_image>( vkCtx.CreateImage( hiZInfo ) );

		hizSrv = vkCtx.AllocDescriptor( vk_descriptor_info{ pHiZTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		for( u64 i = 0; i < pHiZTarget->mipCount; ++i )
		{
			hiZMipViews[ i ] = VkMakeImgView( 
				vkCtx.device, pHiZTarget->hndl, hiZInfo.format, i, 1, VK_IMAGE_VIEW_TYPE_2D, 0, hiZInfo.layerCount );
			hizMipUavs[ i ] = vkCtx.AllocDescriptor( vk_descriptor_info{ hiZMipViews[ i ], VK_IMAGE_LAYOUT_GENERAL } );
		}

		quadMinSampler =
			VkMakeSampler( vkCtx.device, VK_SAMPLER_REDUCTION_MODE_MIN, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE );

		quadMinSamplerIdx = vkCtx.AllocDescriptor( vk_descriptor_info{ quadMinSampler } );
	}

	void Execute( vk_command_buffer& cmdBuff, const vk_image& depthTarget, desc_handle depthIdx )
	{
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
					pHiZTarget->hndl,
					0,0,
					VK_ACCESS_2_SHADER_WRITE_BIT,
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_GENERAL,
					VK_IMAGE_ASPECT_COLOR_BIT )
		};

		cmdBuff.CmdPipelineImageBarriers( hizBeginBarriers );

		cmdBuff.CmdBindPipelineAndBindlessDesc( pipeline, VK_PIPELINE_BIND_POINT_COMPUTE );

		VkMemoryBarrier2 executionBarrier[] = { {
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				//.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
			} };

		u32 mipLevel = 0;
		u32 srcImg = depthIdx.slot;
		for( u64 i = 0; i < pHiZTarget->mipCount; ++i )
		{
			if( i > 0 )
			{
				mipLevel = i - 1;
				srcImg = hizSrv.slot;
			}
			u32 dstImg = hizMipUavs[ i ].slot;

			u32 levelWidth = std::max( 1u, u32( pHiZTarget->width ) >> i );
			u32 levelHeight = std::max( 1u, u32( pHiZTarget->height ) >> i );

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
			push_const pushConst{ vec2{(float)levelWidth, (float)levelHeight}, quadMinSamplerIdx.slot, srcImg, mipLevel, dstImg };
			cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );

			group_size grSz = { 32,32,1 };
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
};

struct render_context
{
	vk_descriptor_allocator descAllocator;

	imgui_context   imguiCtx;
	debug_context   dbgCtx;
	culling_ctx     cullingCtx;
	tonemapping_ctx tonemappingCtx;
	depth_pyramid_pass hizbCtx;


	frame_resources	vrtFrames[ vk_renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED ];

	std::unique_ptr<vk_context> pVkCtx;

	std::shared_ptr<vk_image> pColorTarget;
	std::shared_ptr<vk_image> pDepthTarget;

	// TODO: move to appropriate technique/context
	VkPipeline      gfxZPrepass;
	VkPipeline		gfxPipeline;
	VkPipeline		gfxMeshletPipeline;
	VkPipeline		gfxMergedPipeline;

	

	u64				vFrameIdx = 0;

	desc_handle colSrv;
	desc_handle depthSrv;

	u8				framesInFlight = 2;
	


	VkSampler pbrSampler;


	desc_handle pbrSamplerIdx;

	eastl::fixed_vector<desc_handle, vk_renderer_config::MAX_SWAPCHAIN_IMG_ALLOWED, false> swapchainUavs;


	void InitGlobalResources();
};

void render_context::InitGlobalResources()
{
	if( pDepthTarget == nullptr )
	{
		constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_info info = {
			.name = "Img_DepthTarget",
			.format = renderCfg.desiredDepthFormat,
			.usg = usgFlags,
			.width = pVkCtx->sc.width,
			.height = pVkCtx->sc.height,
			.layerCount = 1,
			.mipCount = 1,
		};
		pDepthTarget = std::make_shared<vk_image>( pVkCtx->CreateImage( info ) );

		depthSrv = pVkCtx->AllocDescriptor( vk_descriptor_info{ pDepthTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
	}
	if( pColorTarget == nullptr )
	{
		constexpr VkImageUsageFlags usgFlags =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image_info info = {
			.name = "Img_ColorTarget",
			.format = renderCfg.desiredColorFormat,
			.usg = usgFlags,
			.width = pVkCtx->sc.width,
			.height = pVkCtx->sc.height,
			.layerCount = 1,
			.mipCount = 1,
		};
		pColorTarget = std::make_shared<vk_image>( pVkCtx->CreateImage( info ) );

		colSrv = pVkCtx->AllocDescriptor( vk_descriptor_info{ pColorTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		pbrSampler = 
			VkMakeSampler( pVkCtx->device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );

		pbrSamplerIdx = pVkCtx->AllocDescriptor( vk_descriptor_info{ pbrSampler } );
	}

	for( const vk_swapchain_image& scImg : pVkCtx->sc.imgs )
	{
		swapchainUavs.push_back( pVkCtx->AllocDescriptor( vk_descriptor_info{ scImg.view, VK_IMAGE_LAYOUT_GENERAL } ) );
	}
}

static render_context rndCtx;

// TODO: separate from render_context
void VkBackendInit( uintptr_t hInst, uintptr_t hWnd )
{
	rndCtx.pVkCtx = std::make_unique<vk_context>( VkMakeContext( hInst, hWnd, renderCfg ) );

	{
		unique_shader_ptr vtx = rndCtx.pVkCtx->CreateShaderFromSpirv( SysReadFile( "Shaders/v_z_prepass.vert.spv" ) );

		vk_gfx_shader_stage shaderStages[] = { *vtx };
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		rndCtx.gfxZPrepass = rndCtx.pVkCtx->CreateGfxPipeline( 
			shaderStages, dynamicStates, 0, 0, renderCfg.desiredDepthFormat, {} );
	}
	//{
	//	vk_shader vertBox = dc.CreateShaderFromSpirv( "Shaders/box_meshlet_draw.vert.spv", rndCtx.pDevice->device );
	//	vk_shader normalCol = dc.CreateShaderFromSpirv( "Shaders/f_pass_col.frag.spv", rndCtx.pDevice->device );
	//
	//	vk_gfx_pipeline_state lineDrawPipelineState = {
	//		.polyMode = VK_POLYGON_MODE_LINE,
	//		.cullFlags = VK_CULL_MODE_NONE,
	//		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
	//		.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
	//		.depthWrite = VK_FALSE,
	//		.depthTestEnable = VK_FALSE,
	//		.blendCol = VK_FALSE
	//	};
	//
	//	dbgDrawProgram = VkMakePipelineProgram( 
	//		rndCtx.pDevice->device, rndCtx.pDevice->gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertBox, &normalCol }, rndCtx..descManager.setLayout );
	//	gfxDrawIndirDbg = VkMakeGfxPipeline(
	//		rndCtx.pDevice->device, 0, dbgDrawProgram.pipeLayout, 
	//		vertBox.module, normalCol.module, 
	//		&renderCfg.desiredColorFormat, 1, VK_FORMAT_UNDEFINED,
	//		lineDrawPipelineState );
	//
	//	vkDestroyShaderModule( rndCtx.pDevice->device, vertBox.module, 0 );
	//	vkDestroyShaderModule( rndCtx.pDevice->device, normalCol.module, 0 );
	//}
	rndCtx.cullingCtx.Init( *rndCtx.pVkCtx );
	rndCtx.tonemappingCtx.Init( *rndCtx.pVkCtx );
	{
		unique_shader_ptr vtx = rndCtx.pVkCtx->CreateShaderFromSpirv( SysReadFile( "Shaders/vtx_merged.vert.spv" ) );
		unique_shader_ptr frag = rndCtx.pVkCtx->CreateShaderFromSpirv( SysReadFile( "Shaders/pbr.frag.spv" ) );
		vk_gfx_pipeline_state opaqueState = {};

		vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		rndCtx.gfxMergedPipeline = rndCtx.pVkCtx->CreateGfxPipeline( 
			shaderStages, dynamicStates, &renderCfg.desiredColorFormat, 1, renderCfg.desiredDepthFormat, opaqueState );
	}
	//{
	//	vk_shader vertMeshlet = dc.CreateShaderFromSpirv( "Shaders/meshlet.vert.spv", rndCtx.pDevice->device );
	//	vk_shader fragCol = dc.CreateShaderFromSpirv( "Shaders/f_pass_col.frag.spv", rndCtx.pDevice->device );
	//	rndCtx.gfxMeshletPipeline = VkMakeGfxPipeline(
	//		rndCtx.pDevice->device, 0, rndCtx.globalLayout, vertMeshlet.module, fragCol.module, 
	//		&renderCfg.desiredColorFormat, 1, renderCfg.desiredDepthFormat, {} );
	//	VkDbgNameObj( rndCtx.gfxMeshletPipeline, rndCtx.pDevice->device, "Pipeline_Gfx_MeshletDraw" );
	//
	//	vkDestroyShaderModule( rndCtx.pDevice->device, vertMeshlet.module, 0 );
	//	vkDestroyShaderModule( rndCtx.pDevice->device, fragCol.module, 0 );
	//}
	rndCtx.hizbCtx.Init( *rndCtx.pVkCtx );

	//rndCtx.dbgCtx.Init( *rndCtx.pDevice, rndCtx.globalLayout, renderCfg );
	//rndCtx.dbgCtx.InitData( *rndCtx.pDevice, 1 * KB, 1 * KB );

	//rndCtx.imguiCtx.Init( *rndCtx.pDevice );
}

#if 0
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

#endif

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

	vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

	cmdBuff.CmdBindPipelineAndBindlessDesc( vkPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS );

	cmdBuff.CmdPushConstants( pPushData, pushDataSize );
	cmdBuff.CmdDrawIndexedIndirectCount( indexBuff, indexType, drawCmds, drawCount, maxDrawCount );
}

inline static void
DepthPyramidMultiPass(
	vk_command_buffer		cmdBuff,
	VkPipeline				vkPipeline,
	
	const vk_image&			depthPyramid,
	u16                     hiZReadIdx,
	const u16*              hiZMipWriteIndices,
	u16                     samplerIdx,
	VkPipelineLayout        pipelineLayout,
	group_size              grSz
) {
	
}

struct drak_file_viewer
{
	const drak_file_footer const* fileFooter;
	std::span<const u8> binaryData;

	drak_file_viewer( std::span<const u8> data ) : binaryData{ data }
	{
		HT_ASSERT( std::size( data ) );
		//binaryData = SysReadFile( drakPath );
		if( std::size( binaryData ) == 0 )
		{
			HT_ASSERT( 0 && "No valid file" );
			//std::vector<u8> fileData = SysReadFile( glbPath );
			//CompileGlbAssetToBinary( fileData, binaryData );
			// TODO: does this override ?
			//SysWriteToFile( drakPath, std::data( binaryData ), std::size( binaryData ) );
		}
		fileFooter = ( drak_file_footer* ) ( std::data( binaryData ) + std::size( binaryData ) - sizeof( drak_file_footer ) );
		HT_ASSERT( std::strcmp( "DRK", fileFooter->magik ) == 0 );
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
		vk_context&          dc, 
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

			//refM.baseColIdx = VkAllocDescriptorIdx( 
			//	rndCtx.descAllocator, vk_descriptor_info{ mBaseCol->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
			//refM.normalMapIdx = VkAllocDescriptorIdx( 
			//	rndCtx.descAllocator, vk_descriptor_info{ mNormalMap->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
			//refM.occRoughMetalIdx = VkAllocDescriptorIdx( 
			//	rndCtx.descAllocator, vk_descriptor_info{ mOccRoughMetal->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL });
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
		vk_context&          dc, 
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
	vk_context& dc,
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
	//HT_ASSERT( "DRK"sv == fileFooter.magik  );

	const std::span<mesh_desc> meshes = { 
		(mesh_desc*) ( std::data( binaryData ) + fileFooter.meshesByteRange.offset ),
		fileFooter.meshesByteRange.size / sizeof( mesh_desc ) };


	std::srand( randSeed );

	std::vector<instance_desc> instDesc = SpawnRandomInstances( { std::data( meshes ),std::size( meshes ) }, drawCount, 1, sceneRad );
	std::vector<light_data> lights = SpawnRandomLights( lightCount, sceneRad * 0.75f );

	HT_ASSERT( std::size( instDesc ) < u16( -1 ) );


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
	const u64 currentFrameInFlightIdx = currentFrameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;
	frame_resources& thisVFrame = rndCtx.vrtFrames[ currentFrameInFlightIdx ];

	// TODO: when we add async compute this must be adjusted to account for the extra singal(s)
	VkResult timelineWaitResult = rndCtx.pVkCtx->TryWaitOnTimelineFor( 
		rndCtx.pVkCtx->frameTimeline, rndCtx.framesInFlight, UINT64_MAX );
	HT_ASSERT( timelineWaitResult < VK_TIMEOUT );

	[[unlikely]]
	if( currentFrameIdx < rndCtx.framesInFlight )
	{
		thisVFrame.pViewData = std::make_shared<vk_buffer>( rndCtx.pVkCtx->CreateBuffer( {
			.name = "Buff_VirtualFrame_ViewBuff",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.elemCount = std::size( frameData.views ),
			.stride = sizeof( view_data ),
			.usage = buffer_usage::HOST_VISIBLE
		} ) );
		thisVFrame.viewDataIdx = rndCtx.pVkCtx->AllocDescriptor( vk_descriptor_info{ *thisVFrame.pViewData } );
	}
	HT_ASSERT( thisVFrame.pViewData->sizeInBytes == BYTE_COUNT( frameData.views ) );
	std::memcpy( thisVFrame.pViewData->hostVisible, std::data( frameData.views ), BYTE_COUNT( frameData.views ) );

	vk_command_buffer thisFrameCmdBuffer = rndCtx.pVkCtx->GetFrameCmdBuff( currentFrameInFlightIdx );

	u32 scImgIdx = rndCtx.pVkCtx->AcquireNextSwapchainImageBlocking( currentFrameInFlightIdx );
	const vk_swapchain_image& scImg = rndCtx.pVkCtx->sc.imgs[ scImgIdx ];

	static bool initResources = false;
	if( !initResources )
	{
		rndCtx.InitGlobalResources();

		//VkUploadResources( *vk.pDc, stagingManager, thisFrameCmdBuffer, entities, currentFrameIdx );

		u32 instCount = 1; instDescBuff.sizeInBytes / sizeof( instance_desc );
		u32 mletCount = 1; meshletBuff.sizeInBytes / sizeof( meshlet );
		rndCtx.cullingCtx.InitSceneDependentData( *rndCtx.pVkCtx, instCount, mletCount * instCount );

		//rndCtx.dbgCtx.UploadDebugGeometry();

		//rndCtx.imguiCtx.InitResources( *vk.pDc, renderCfg.desiredSwapchainFormat );

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

	rndCtx.pVkCtx->FlushPendingDescriptorUpdates();

	const vk_image& depthTarget = *rndCtx.pDepthTarget;
	const vk_image& colorTarget = *rndCtx.pColorTarget;

	auto depthWrite = VkMakeAttachemntInfo( depthTarget.view, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto depthRead = VkMakeAttachemntInfo( depthTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorWrite = VkMakeAttachemntInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorRead = VkMakeAttachemntInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );

	VkViewport viewport = VkGetSwapchainViewport( rndCtx.pVkCtx->sc );
	VkRect2D scissor = VkGetSwapchianScissor( rndCtx.pVkCtx->sc );

	u32 instCount = instDescBuff.sizeInBytes / sizeof( instance_desc );
	u32 mletCount = meshletBuff.sizeInBytes / sizeof( meshlet );
	u32 meshletUpperBound = instCount * mletCount;

	DirectX::XMMATRIX t = DirectX::XMMatrixMultiply( 
		DirectX::XMMatrixScaling( 180.0f, 100.0f, 60.0f ), DirectX::XMMatrixTranslation( 20.0f, -10.0f, -60.0f ) );
	DirectX::XMFLOAT4X4A debugOcclusionWallTransf;
	DirectX::XMStoreFloat4x4A( &debugOcclusionWallTransf, t );

	DirectX::XMUINT2 colorTargetSize = { colorTarget.width, colorTarget.height };

	//VkResetGpuTimer( thisVFrame.cmdBuff, thisVFrame.gpuTimer );

	{
		//vk_time_section timePipeline = { thisVFrame.cmdBuff, thisVFrame.gpuTimer.queryPool, 0 };
		rndCtx.cullingCtx.Execute( thisFrameCmdBuffer, *rndCtx.hizbCtx.pHiZTarget, instCount, thisVFrame.viewDataIdx.slot, 
								   rndCtx.hizbCtx.hizSrv.slot, rndCtx.hizbCtx.quadMinSamplerIdx.slot, false );

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
				.camIdx = thisVFrame.viewDataIdx.slot,
				.samplerIdx = rndCtx.pbrSamplerIdx.slot
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
		//rndCtx.dbgCtx.DrawCPU( thisFrameCmdBuffer, zDgbInfo, "Draw Occluder-Depth", debug_draw_type::TRIANGLE, 
		//					   thisVFrame.pViewData->devicePointer, 0, debugOcclusionWallTransf, 0 );

		rndCtx.hizbCtx.Execute( thisFrameCmdBuffer, depthTarget, rndCtx.depthSrv );

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

		rndCtx.cullingCtx.Execute( thisFrameCmdBuffer, *rndCtx.hizbCtx.pHiZTarget, instCount, thisVFrame.viewDataIdx.slot, 
								   rndCtx.hizbCtx.hizSrv.slot, rndCtx.hizbCtx.quadMinSamplerIdx.slot, true );

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

		rndCtx.hizbCtx.Execute( thisFrameCmdBuffer, depthTarget, rndCtx.depthSrv );

		VkRenderingAttachmentInfo attInfosDbg[] = { colorRead };
		vk_rendering_info colDgbInfo = {
			.viewport = viewport,
			.scissor = scissor,
			.colorAttachments = attInfosDbg,
			.pDepthAttachment = 0
		};
		//rndCtx.dbgCtx.DrawCPU( thisFrameCmdBuffer, colDgbInfo, "Draw Occluder-Color", debug_draw_type::TRIANGLE, 
		//					   thisVFrame.pViewData->devicePointer, 1, debugOcclusionWallTransf, cyan );

		if( frameData.freezeMainView )
		{
			VkRenderingAttachmentInfo attInfosDbg[] = { colorRead };
			vk_rendering_info colDgbInfo = {
				.viewport = viewport,
				.scissor = scissor,
				.colorAttachments = attInfosDbg,
				.pDepthAttachment = 0
			};
			//rndCtx.dbgCtx.DrawCPU( thisFrameCmdBuffer, colDgbInfo, "Draw Frustum", debug_draw_type::LINE, 
			//					   thisVFrame.pViewData->devicePointer, 1, frameData.frustTransf, yellow );
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
			thisFrameCmdBuffer, frameData.elapsedSeconds, rndCtx.colSrv.slot, colorTargetSize );

		// NOTE: we need exec dependency from acquireImgKHR ( col out + compute shader ) to compute shader
		VkImageMemoryBarrier2 scWriteBarrier[] = { 
			VkMakeImageBarrier2( scImg.hndl,
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


		HT_ASSERT( ( colorTarget.width == rndCtx.pVkCtx->sc.width ) && 
			( colorTarget.height == rndCtx.pVkCtx->sc.height ) );

		rndCtx.tonemappingCtx.TonemappingGammaPass( 
			thisFrameCmdBuffer, rndCtx.colSrv.slot, rndCtx.swapchainUavs[ scImgIdx ].slot, colorTargetSize );

		VkImageMemoryBarrier2 compositionEndBarriers[] = {
			VkMakeImageBarrier2( colorTarget.hndl,
								 VK_ACCESS_2_SHADER_READ_BIT,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 0, 0,
								 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ),
			VkMakeImageBarrier2( scImg.hndl,
							  VK_ACCESS_2_SHADER_WRITE_BIT,
							  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
							  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
							  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
							  VK_IMAGE_LAYOUT_GENERAL,
							  VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
							  VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineImageBarriers( compositionEndBarriers );


		VkViewport uiViewport = { 0, 0, ( float ) rndCtx.pVkCtx->sc.width, ( float ) rndCtx.pVkCtx->sc.height, 0, 1.0f };
		vkCmdSetViewport( thisFrameCmdBuffer.hndl, 0, 1, &uiViewport );

		auto swapchainUIRW = VkMakeAttachemntInfo( 
			scImg.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {});
		//rndCtx.imguiCtx.DrawUiPass( thisVFrame.cmdBuff, &swapchainUIRW, 0, scissor, currentFrameIdx );


		VkImageMemoryBarrier2 presentWaitBarrier[] = { 
			VkMakeImageBarrier2( scImg.hndl,
								 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
								 0, 0,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineImageBarriers( presentWaitBarrier );
	}

	//gpuData.timeMs = VkCmdReadGpuTimeInMs( thisVFrame.cmdBuff, thisVFrame.gpuTimer );
	thisFrameCmdBuffer.CmdEndCmbBuffer();

	// NOTE: with all these cool stage masks we can only let the gpu run until it need the sc image THEN wait
	VkSemaphoreSubmitInfo waitScImgAcquire = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = rndCtx.pVkCtx->vrtFrames[ currentFrameInFlightIdx ].canGetImgSema,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	};

	VkSemaphoreSubmitInfo signalRenderFinished = {
		.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = scImg.canPresentSema, 
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
	};

	VkSemaphoreSubmitInfo signalTimeline = {
		.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = rndCtx.pVkCtx->frameTimeline.sema,
		.value     = rndCtx.vFrameIdx,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
	};
	rndCtx.pVkCtx->frameTimeline.submitionCount++;

	VkSemaphoreSubmitInfo waits[] = { waitScImgAcquire };
	VkSemaphoreSubmitInfo signals[] = { signalRenderFinished, signalTimeline };
	rndCtx.pVkCtx->QueueSubmit( rndCtx.pVkCtx->gfxQueue, waits, signals, thisFrameCmdBuffer.hndl );

	rndCtx.pVkCtx->QueuePresent( rndCtx.pVkCtx->gfxQueue, scImgIdx );
}

void VkBackendKill()
{
}

#undef HTVK_NO_SAMPLER_REDUCTION
#undef VK_APPEND_DESTROYER
#undef VK_CHECK