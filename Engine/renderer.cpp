#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <Volk/volk.h>

#include <cstdarg>
#include <string.h>
#include <string_view>
#include <span>
#include <format>
#include <memory>

#include "core_types.h"
#include "ht_utils.h"
#include "sys_os_api.h"

#include "vk_error.h"
#include "vk_resources.h"
#include "vk_sync.h"
#include "vk_pso.h"

#include "vk_context.h"

#include "ht_fixed_vector.h"
#include "ht_fixed_string.h"

#include <DirectXPackedVector.h>

namespace DXPacked = DirectX::PackedVector;


//====================CONSTS====================//
static constexpr u64 MAX_FIF = vk_renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED;
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

#include "r_data_structs.h"

// NOTE: clear depth to 0 bc we	use RevZ
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

#include "ht_geometry.h"


inline u32 GroupCount( u32 invocationCount, u32 workGroupSize )
{
	return ( invocationCount + workGroupSize - 1 ) / workGroupSize;
}

#include <imgui.h>

struct imgui_pass
{
	using index_t = u16;

	static_assert( sizeof( ImDrawVert ) == sizeof( imgui_vertex ) );
	static_assert( sizeof( ImDrawIdx ) == sizeof( index_t ) );

	static constexpr u64 DEFAULT_BUFF_SIZE = 2 * KB;

	fixed_vector<HVKBUF, MAX_FIF>		vtx;
	fixed_vector<HVKBUF, MAX_FIF>		idx;
	HVKIMG								fontAtlasImg;
	VkSampler								fontSampler;

	VkDescriptorSetLayout					descSetLayout;
	VkPipelineLayout						pipelineLayout;
	VkDescriptorUpdateTemplate				descTemplate;
	VkPipeline								pipeline;

	void CreateUploadFontAtlasSync( vk_context& dc, vk_command_buffer& cmdBuff, u64 frameIdx )
	{
		u8* pixels = 0;
		i32 width = 0, height = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );

		constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		fontAtlasImg = dc.CreateImage( {
			.name = "Img_ImGuiFonts",
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.usg = usgFlags,
			.width = ( u16 ) width,
			.height = ( u16 ) height,
			.layerCount = 1,
			.mipCount = 1,
		} );

		u64 sizeInBytes = width * height * 4;

		HVKBUF stagingBuff = dc.CreateBuffer( {
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.sizeInBytes = sizeInBytes,
			.usage = buffer_usage::STAGING
		} );

		std::memcpy( stagingBuff->hostVisible, pixels, sizeInBytes );

		cmdBuff.CmdPipelineImageBarriers( VkMakeImageBarrier(
			*fontAtlasImg, {}, { VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT },
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkFullResource( *fontAtlasImg )
		) );
		cmdBuff.CmdCopyBufferToImageSubresource( *stagingBuff, 0, *fontAtlasImg, VkFullResourceLayers( *fontAtlasImg ) );
		cmdBuff.CmdPipelineImageBarriers( VkMakeImageBarrier(
			*fontAtlasImg, { VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT }, {}, 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VkFullResource( *fontAtlasImg )
		) );

		dc.EnqueueResourceFree( vk_resc_deletion{ stagingBuff, frameIdx } );
	}

	void ReallocBuffers(
		vk_context& vkCtx, 
		u64 frameIdx, 
		u64 frameInFlightIdx, 
		u64 vtxTotalSizeInBytes, 
		u64 idxTotalSizeInBytes 
	) {
		[[unlikely]] while( std::size( vtx ) <= frameInFlightIdx ) vtx.push_back( {} );
		[[unlikely]] while( std::size( idx ) <= frameInFlightIdx ) idx.push_back( {} );

		if( IsValidHandle( vtx[ frameInFlightIdx ] ) &&
			vtx[ frameInFlightIdx ]->sizeInBytes >= vtxTotalSizeInBytes ) return;

		if( IsValidHandle( vtx[ frameInFlightIdx ] ) )
		{
			vkCtx.EnqueueResourceFree( vk_resc_deletion{ vtx[ frameInFlightIdx ], frameIdx } );
		}

		vtx[ frameInFlightIdx ] = vkCtx.CreateBuffer( {
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			.sizeInBytes = vtxTotalSizeInBytes,
			.usage = buffer_usage::HOST_VISIBLE 
		} );

		if( IsValidHandle( idx[ frameInFlightIdx ] ) &&
			idx[ frameInFlightIdx ]->sizeInBytes >= idxTotalSizeInBytes ) return;

		if( IsValidHandle( idx[ frameInFlightIdx ] ) )
		{
			vkCtx.EnqueueResourceFree( vk_resc_deletion{ idx[ frameInFlightIdx ], frameIdx } );
		}

		idx[ frameInFlightIdx ] = vkCtx.CreateBuffer( {
			.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			.sizeInBytes = idxTotalSizeInBytes,
			.usage = buffer_usage::HOST_VISIBLE 
		} );
	}

	// NOTE: it's mostly inspired by the official backend code
	void DrawUiPass(
		vk_context& vkCtx,
		VkCommandBuffer cmdBuff,
		const vk_image& dstTarget,
		u64 frameIdx,
		u64 frameInFlightIdx
	) {
		HT_ASSERT( frameInFlightIdx < vtx.capacity() );
		HT_ASSERT( frameInFlightIdx < idx.capacity() );

		const ImDrawData* drawData = ImGui::GetDrawData();

		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		float fbWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
		float fbHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
		if( fbWidth <= 0.0f || fbHeight <= 0.0f ) return;

		// Textrues

		if( drawData->TotalVtxCount > 0 )
		{
			u64 vtxTotalSizeInBytes = std::bit_ceil( drawData->TotalVtxCount * sizeof( imgui_vertex ) );
			u64 idxTotalSizeInBytes = std::bit_ceil( drawData->TotalVtxCount * sizeof( index_t ) );

			ReallocBuffers( vkCtx, frameIdx, frameInFlightIdx, vtxTotalSizeInBytes, idxTotalSizeInBytes );
		}

		const vk_buffer& vtxBuff = *vtx[ frameInFlightIdx ];
		const vk_buffer& idxBuff = *idx[ frameInFlightIdx ];

		ImDrawVert* vtxDst = ( ImDrawVert* ) vtxBuff.hostVisible;
		ImDrawIdx* idxDst = ( ImDrawIdx* ) idxBuff.hostVisible;
		for( const ImDrawList* cmdList : drawData->CmdLists )
		{
			std::memcpy( vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof( imgui_vertex ) );
			std::memcpy( idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof( index_t ) );
			vtxDst += cmdList->VtxBuffer.Size;
			idxDst += cmdList->IdxBuffer.Size;
		}

		vec2 scale = { 2.0f / drawData->DisplaySize.x, 2.0f / drawData->DisplaySize.y };
		vec2 move = { -1.0f - drawData->DisplayPos.x * scale.x, -1.0f - drawData->DisplayPos.y * scale.y };
		vec4 pushConst = { scale.x, scale.y, move.x, move.y };

		VkDescriptorImageInfo descImgInfo = { fontSampler, fontAtlasImg->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
		vk_descriptor_info pushDescs[] = { Descriptor( vtxBuff ), descImgInfo };

		vk_scoped_label label = { cmdBuff,"Draw Imgui Pass",{} };

		VkRect2D renderArea = VkGetScissor( dstTarget.width, dstTarget.height );

		// NOTE: we need a different viewport since this is drawn directly to the screen
		VkViewport uiViewport = { 0, 0, ( float ) dstTarget.width, ( float ) dstTarget.height, 0, 1.0f };
		vkCmdSetViewport( cmdBuff, 0, 1, &uiViewport );

		VkRenderingAttachmentInfo dstTargetAttachmentInfo = VkMakeAttachemntInfo( 
			dstTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
		VkRenderingInfo renderInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = renderArea,
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &dstTargetAttachmentInfo,
		};
		vk_scoped_renderpass renderPass = { cmdBuff, renderInfo };

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, descTemplate, pipelineLayout, 0, pushDescs );
		vkCmdPushConstants( cmdBuff, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( pushConst ), &pushConst );
		vkCmdBindIndexBuffer( cmdBuff, idxBuff.hndl, 0, VK_INDEX_TYPE_UINT16 );

		// (0,0) unless using multi-viewports
		vec2 clipOff = { drawData->DisplayPos.x, drawData->DisplayPos.y };
		// (1,1) unless using retina display which are often (2,2)
		vec2 clipScale = { drawData->FramebufferScale.x, drawData->FramebufferScale.y };

		u32 vtxOffset = 0, idxOffset = 0;
		for( u32 li = 0u; li < ( u32 ) drawData->CmdListsCount; ++li )
		{
			const ImDrawList* cmdList = drawData->CmdLists[ li ];
			for( u32 ci = 0u; ci < ( u32 ) cmdList->CmdBuffer.Size; ++ci )
			{
				const ImDrawCmd* pCmd = &cmdList->CmdBuffer[ ci ];
				// Project scissor/clipping rectangles into framebuffer space
				vec2 clipMin = { ( pCmd->ClipRect.x - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.y - clipOff.y ) * clipScale.y };
				vec2 clipMax = { ( pCmd->ClipRect.z - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.w - clipOff.y ) * clipScale.y };

				// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
				clipMin = { std::max( clipMin.x, 0.0f ), std::max( clipMin.y, 0.0f ) };
				clipMax = { std::min( clipMax.x, ( float ) renderArea.extent.width ), 
					std::min( clipMax.y, ( float ) renderArea.extent.height ) };

				if( clipMax.x < clipMin.x || clipMax.y < clipMin.y ) continue;

				VkRect2D scissor = { i32( clipMin.x ), i32( clipMin.y ), u32( clipMax.x - clipMin.x ), u32( clipMax.y - clipMin.y ) };
				vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

				vkCmdDrawIndexed( cmdBuff, pCmd->ElemCount, 1, pCmd->IdxOffset + idxOffset, pCmd->VtxOffset + vtxOffset, 0 );
			}
			idxOffset += cmdList->IdxBuffer.Size;
			vtxOffset += cmdList->VtxBuffer.Size;
		}
	}

};

imgui_pass MakeImguiPass( vk_context& dc, VkFormat colDstFormat )
{
	VkSamplerCreateInfo samplerCreateInfo = { 
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxAnisotropy = 1.0f,
		.minLod = 0,
		.maxLod = VK_LOD_CLAMP_NONE,
		.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
	};

	VkSampler fontSampler = dc.CreateSampler( samplerCreateInfo );

	VkDescriptorSetLayoutBinding descSetBindings[ 2 ] = {};
	descSetBindings[ 0 ].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descSetBindings[ 0 ].descriptorCount = 1;
	descSetBindings[ 0 ].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	descSetBindings[ 0 ].binding = 0;
	descSetBindings[ 1 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descSetBindings[ 1 ].descriptorCount = 1;
	descSetBindings[ 1 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	descSetBindings[ 1 ].pImmutableSamplers = &fontSampler;
	descSetBindings[ 1 ].binding = 1;

	VkDescriptorSetLayoutCreateInfo descSetInfo = { 
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
		.bindingCount = std::size( descSetBindings ),
		.pBindings = descSetBindings
	};

	VkDescriptorSetLayout descSetLayout = {};
	VK_CHECK( vkCreateDescriptorSetLayout( dc.device, &descSetInfo, 0, &descSetLayout ) );

	VkPushConstantRange pushConst = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( float ) * 4 };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConst
	};

	VkPipelineLayout pipelineLayout = {};
	VK_CHECK( vkCreatePipelineLayout( dc.device, &pipelineLayoutInfo, 0, &pipelineLayout ) );

	VkDescriptorUpdateTemplateEntry entries[ std::size( descSetBindings ) ] = {};
	for( u64 bi = 0; bi < std::size( descSetBindings ); ++bi )
	{
		VkDescriptorSetLayoutBinding bindingLayout = descSetBindings[ bi ];
		entries[ bi ] = {
			.dstBinding = bindingLayout.binding,
			.descriptorCount = 1, 
			.descriptorType = bindingLayout.descriptorType,
			.offset = bi * sizeof( vk_descriptor_info ),
			.stride = sizeof( vk_descriptor_info ),
		};
	}

	VkDescriptorUpdateTemplateCreateInfo templateInfo = { 
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
		.descriptorUpdateEntryCount = std::size( entries ),
		.pDescriptorUpdateEntries = std::data( entries ),
		.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR,
		.descriptorSetLayout = descSetLayout,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.pipelineLayout = pipelineLayout
	};

	VkDescriptorUpdateTemplate descTemplate = {};
	VK_CHECK( vkCreateDescriptorUpdateTemplate( dc.device, &templateInfo, 0, &descTemplate ) );

	unique_shader_ptr vtx = dc.CreateShaderFromSpirv( 
		SysReadFile( "../bin/SpirV/vertex_ImGuiVsMain.spirv" ) );
	unique_shader_ptr frag = dc.CreateShaderFromSpirv( 
		SysReadFile( "../bin/SpirV/pixel_ImGuiPsMain.spirv" ) );

	vk_gfx_pso_config guiState = {
		.polyMode = VK_POLYGON_MODE_FILL,
		.cullFlags = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.depthWrite = false,
		.depthTestEnable = false,
		.blendCol = true
	};

	vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipeline pipeline = dc.CreateGfxPipeline(
		shaderStages, dynamicStates, &colDstFormat, 1, VK_FORMAT_UNDEFINED, guiState, pipelineLayout );

	return {
		.fontSampler = fontSampler,
		.descSetLayout = descSetLayout,
		.pipelineLayout = pipelineLayout,
		.descTemplate = descTemplate,
		.pipeline = pipeline
	};
}

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
struct debug_draw_passes
{
	HVKBUF hLinesBuff;
	HVKBUF hTrisBuff;
	HVKBUF hDrawCount;

	VkPipeline	drawAsLines;
	VkPipeline	drawAsTriangles;

	void Init( vk_context& dc, VkPipelineLayout globalLayout, vk_renderer_config& rndCfg )
	{
		unique_shader_ptr vtx = dc.CreateShaderFromSpirv( 
			SysReadFile( "../Shaders/v_cpu_dbg_draw.vert.spv" ) );
		unique_shader_ptr frag = dc.CreateShaderFromSpirv( 
			SysReadFile( "../Shaders/f_pass_col.frag.spv" ) );

		static_assert( worldLeftHanded );

		{
			vk_gfx_pso_config lineDrawPipelineState = {
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
			vk_gfx_pso_config triDrawPipelineState = {
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

		constexpr VkBufferUsageFlags usgFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		hDrawCount = dc.CreateBuffer( {
			.name = "Buff_DbgDrawCount",
			.usageFlags = usgFlags,
			.sizeInBytes = 1 * sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } );  
	}

	void InitData( vk_context& dc, u32 dbgLinesSizeInBytes, u32 dbgTrianglesSizeInBytes )
	{
		hLinesBuff = dc.CreateBuffer( { 
			.name = "Buff_DbgLines",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
			.sizeInBytes = dbgLinesSizeInBytes, 
			.usage = buffer_usage::HOST_VISIBLE } );

		hTrisBuff = dc.CreateBuffer( { 
			.name = "Buff_DbgTris",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
			.sizeInBytes = dbgTrianglesSizeInBytes, 
			.usage = buffer_usage::HOST_VISIBLE } );
	}

	// NOTE: must be aligned properly to work
	void UploadDebugGeometry()
	{
		auto unitCube = GenerateBoxWithBounds( BOX_MIN, BOX_MAX );
		auto lineVtxBuff = BoxVerticesAsLines( unitCube );
		auto trisVtxBuff = BoxVerticesAsTriangles( unitCube );

		HT_ASSERT( hLinesBuff->sizeInBytes >= BYTE_COUNT( lineVtxBuff ) );
		HT_ASSERT( hTrisBuff->sizeInBytes >= BYTE_COUNT( trisVtxBuff ) );
		std::memcpy( hLinesBuff->hostVisible, std::data( lineVtxBuff ), BYTE_COUNT( lineVtxBuff ) );
		std::memcpy( hTrisBuff->hostVisible, std::data( trisVtxBuff ), BYTE_COUNT( trisVtxBuff ) );
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

		u64 vtxAddr = ( ddType == debug_draw_type::TRIANGLE ) ? hTrisBuff->devicePointer : hLinesBuff->devicePointer;
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

static vk_buffer materialsBuff;
static vk_buffer instDescBuff;
static vk_buffer lightsBuff;

constexpr char glbPath[] = "D:\\3d models\\cyberbaron\\cyberbaron.glb";
constexpr char drakPath[] = "Assets/cyberbaron.drak";


struct culling_pass
{
	HVKBUF hInstanceOccludedCache;
	HVKBUF hClusterOccludedCache;
	HVKBUF hCompactedDrawArgs;
	HVKBUF hDrawCmds;
	HVKBUF hDrawCount;
	HVKBUF hAtomicWgCounter;
	HVKBUF hDispatchIndirect;

	VkPipeline		compPipeline;
	VkPipeline      compClusterCullPipe;

	void Init( vk_context& dc )
	{
		unique_shader_ptr drawCull = dc.CreateShaderFromSpirv( SysReadFile( "../Shaders/c_draw_cull.comp.spv" ) );
		compPipeline = dc.CreateComptuePipeline( *drawCull, "Pipeline_Comp_DrawCull" );

		//vk_shader clusterCull = dc.CreateShaderFromSpirv( "Shaders/c_meshlet_cull.comp.spv", dc.device );
		//compClusterCullPipe = VkMakeComputePipeline( 
		//	dc.device, 0, pipelineLayout, clusterCull.module, {}, dc.waveSize, SHADER_ENTRY_POINT, "Pipeline_Comp_ClusterCull" );
		constexpr VkBufferUsageFlags usgFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		hDrawCount = dc.CreateBuffer( {
			.name = "Buff_DrawCount",
			.usageFlags = usgFlags,
			.sizeInBytes = 1 * sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } );

		hAtomicWgCounter = dc.CreateBuffer( {
			.name = "Buff_AtomicWgCounter",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
																		 .sizeInBytes = 1 * sizeof( u32 ),
																		 .usage = buffer_usage::GPU_ONLY } ); 

		hDispatchIndirect = dc.CreateBuffer( {
			.name = "Buff_DispatchIndirect",
			.usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
																		  .sizeInBytes = 1 * sizeof( dispatch_command ),
																		  .usage = buffer_usage::GPU_ONLY } );  
	}

	void InitSceneDependentData( vk_context& dc, u32 instancesUpperBound, u32 meshletUpperBound )
	{
		constexpr VkBufferUsageFlags usg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		hInstanceOccludedCache = dc.CreateBuffer( {
			.name = "Buff_InstanceVisibilityCache",
			.usageFlags = usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes = instancesUpperBound * sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } );

		hClusterOccludedCache = dc.CreateBuffer( {
			.name = "Buff_ClusterVisibilityCache",
			.usageFlags = usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes = meshletUpperBound * sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } );

		hCompactedDrawArgs = dc.CreateBuffer( {
			.name = "Buff_CompactedDrawArgs",
			.usageFlags = usg,
			.sizeInBytes = meshletUpperBound * sizeof( compacted_draw_args ),
			.usage = buffer_usage::GPU_ONLY } );
		hDrawCmds = dc.CreateBuffer( {
			.name = "Buff_DrawCmds",
			.usageFlags = usg | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT ,
			.sizeInBytes = meshletUpperBound * sizeof( draw_command ),
			.usage = buffer_usage::GPU_ONLY } );
	}

	void Execute(
		vk_command_buffer&  cmdBuff, 
		vk_rsc_state_tracker& rscTracker,
		const vk_image&			depthPyramid,
		u32 instCount,
		desc_hndl32 camDesc,
		desc_hndl32 hizDesc,
		desc_hndl32 samplerDesc,
		bool latePass
	) {
		// NOTE: wtf Vulkan ?
		constexpr u64 VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Cull Pass",{} );

		if( !latePass )
		{
			//cmdBuff.CmdFillVkBuffer( *pInstanceOccludedCache, 0u );
		}

		rscTracker.UseBuffer( *hDrawCount, { VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT } );
		rscTracker.UseBuffer( *hAtomicWgCounter, { VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT } );

		cmdBuff.CmdFillVkBuffer( *hDrawCount, 0u );
		cmdBuff.CmdFillVkBuffer( *hAtomicWgCounter, 0u );

		rscTracker.UseBuffer( *hDrawCmds, { VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		rscTracker.UseBuffer( *hDrawCount, { HT_SHADER_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		rscTracker.UseBuffer( *hDispatchIndirect, 
			{ VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		rscTracker.UseBuffer( *hAtomicWgCounter, { HT_SHADER_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		if( !latePass )
		{
			rscTracker.UseBuffer( *hInstanceOccludedCache, 
				{ HT_SHADER_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		}
		rscTracker.UseImage( depthPyramid,
			{ VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT }, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );

		rscTracker.FlushBarriers( cmdBuff );

		cmdBuff.CmdBindPipelineAndBindlessDesc( compPipeline, VK_PIPELINE_BIND_POINT_COMPUTE );

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
				.drawCmdsAddr = hDrawCmds->devicePointer,
				.compactedArgsAddr = hCompactedDrawArgs->devicePointer,
				.instOccCacheAddr = hInstanceOccludedCache->devicePointer,
				.atomicWorkgrCounterAddr = hAtomicWgCounter->devicePointer,
				.visInstaceCounterAddr = hDrawCount->devicePointer,
				.dispatchCmdAddr = hDispatchIndirect->devicePointer,
				.hizBuffIdx = hizDesc.slot,
				.hizSamplerIdx = samplerDesc.slot,
				.instanceCount = instCount,
				.camIdx = camDesc.slot,
				.latePass = ( u32 ) latePass
			};

			cmdBuff.CmdBindPipelineAndBindlessDesc( compPipeline, VK_PIPELINE_BIND_POINT_COMPUTE );
			cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
			vkCmdDispatch( cmdBuff.hndl, GroupCount( instCount, 32 ), 1, 1 );
		}

		rscTracker.UseBuffer( *hDrawCmds, { VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT } );
		rscTracker.UseBuffer( *hCompactedDrawArgs, 
			{ VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT, 
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT } );
		rscTracker.UseBuffer( *hDrawCount, { VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT } );

		rscTracker.FlushBarriers( cmdBuff );
	}
};

struct tone_mapping_pass
{
	HVKBUF	hAverageLuminanceBuffer;
	HVKBUF	hLuminanceHistogramBuffer;
	HVKBUF	hAtomicWgCounterBuff;

	VkPipeline					compAvgLumPipe;
	VkPipeline					compTonemapPipe;

	desc_hndl32					avgLumIdx;
	desc_hndl32					atomicWgCounterIdx;
	desc_hndl32					lumHistoIdx;

	void Init( vk_context& dc )
	{
		unique_shader_ptr avgLum = dc.CreateShaderFromSpirv( 
			SysReadFile( "../bin/SpirV/compute_AvgLuminanceCsMain.spirv" ) );
		compAvgLumPipe = dc.CreateComptuePipeline( *avgLum, "Pipeline_Comp_AvgLum" );

		unique_shader_ptr toneMapper = dc.CreateShaderFromSpirv( 
			SysReadFile( "../bin/SpirV/compute_TonemappingGammaCsMain.spirv" ) );
		compTonemapPipe = dc.CreateComptuePipeline( *toneMapper, "Pipeline_Comp_Tonemapping" );

		VkBufferUsageFlags usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		hAverageLuminanceBuffer = dc.CreateBuffer( {
			.name = "Buff_AvgLum",
			.usageFlags = usageFlags,
			.sizeInBytes = 1 * sizeof( float ),
			.usage = buffer_usage::GPU_ONLY } );
		avgLumIdx = dc.AllocDescriptor( vk_descriptor_info{ *hAverageLuminanceBuffer } );

		hAtomicWgCounterBuff = dc.CreateBuffer( {
			.name = "Buff_TonemappingAtomicWgCounter",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes = 1 * sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } );
		atomicWgCounterIdx = dc.AllocDescriptor( vk_descriptor_info{ *hAtomicWgCounterBuff } );

		hLuminanceHistogramBuffer = dc.CreateBuffer( {
			.name = "Buff_LumHisto",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes = 4 * sizeof( u64 ),
			.usage = buffer_usage::GPU_ONLY } ); 
		lumHistoIdx = dc.AllocDescriptor( vk_descriptor_info{ *hLuminanceHistogramBuffer } );
	}

	void AverageLuminancePass( 
		vk_command_buffer&  cmdBuff,
		float               dt, 
		desc_hndl32		    hdrColSrcDesc,
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
		} pushConst = { avgLumInfo, hdrColSrcDesc.slot, lumHistoIdx.slot, atomicWgCounterIdx.slot, avgLumIdx.slot };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		cmdBuff.CmdDispatch( numWorkGrs );
	}

	void TonemappingGammaPass(
		vk_command_buffer& cmdBuff,
		desc_hndl32         hdrColDesc,
		desc_hndl32         sdrColDesc,
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
		} pushConst = { hdrColDesc.slot, sdrColDesc.slot, avgLumIdx.slot };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		cmdBuff.CmdDispatch( numWorkGrs );
	}
};

struct depth_pyramid_pass
{
	VkPipeline		pipeline;
	HVKIMG			hHiZTarget;
	VkImageView		hiZMipViews[ MAX_MIP_LEVELS ];

	VkSampler       quadMinSampler;

	desc_hndl32     hizSrv;

	desc_hndl32		hizMipUavs[ MAX_MIP_LEVELS ];
	desc_hndl32		quadMinSamplerIdx;

	void Init( vk_context& vkCtx )
	{
		unique_shader_ptr downsampler = vkCtx.CreateShaderFromSpirv( 
			SysReadFile( "../bin/SpirV/compute_Pow2DownSamplerCsMain.spirv" ) );
		pipeline = vkCtx.CreateComptuePipeline( *downsampler, "Pipeline_Comp_HiZ" );

		u16 squareDim = 512;
		u8 hiZMipCount = GetImgMipCount( squareDim, squareDim, MAX_MIP_LEVELS );

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

		hHiZTarget = vkCtx.CreateImage( hiZInfo );

		hizSrv = vkCtx.AllocDescriptor( vk_descriptor_info{ hHiZTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		for( u32 i = 0; i < hHiZTarget->mipCount; ++i )
		{
			hiZMipViews[ i ] = VkMakeImgView( 
				vkCtx.device, hHiZTarget->hndl, hiZInfo.format, i, 1, VK_IMAGE_VIEW_TYPE_2D, 0, hiZInfo.layerCount );
			hizMipUavs[ i ] = vkCtx.AllocDescriptor( vk_descriptor_info{ hiZMipViews[ i ], VK_IMAGE_LAYOUT_GENERAL } );
		}

		VkSamplerReductionModeCreateInfo reduxInfo = { 
			.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
			.reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN,
		};

		VkSamplerCreateInfo samplerCreateInfo = { 
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.pNext = &reduxInfo,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.maxAnisotropy = 1.0f,
			.minLod = 0,
			.maxLod = VK_LOD_CLAMP_NONE,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};

		quadMinSampler = vkCtx.CreateSampler( samplerCreateInfo );
		quadMinSamplerIdx = vkCtx.AllocDescriptor( vk_descriptor_info{ quadMinSampler } );
	}

	void Execute( 
		vk_command_buffer& cmdBuff, 
		vk_rsc_state_tracker& rscTracker, 
		const vk_image& depthTarget, 
		desc_hndl32 depthIdx 
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "HiZ Multi Pass", {} );

		rscTracker.UseImage( depthTarget, { VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT }, 
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
		rscTracker.UseImage( *hHiZTarget, { VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT }, 
			VK_IMAGE_LAYOUT_GENERAL );

		cmdBuff.CmdBindPipelineAndBindlessDesc( pipeline, VK_PIPELINE_BIND_POINT_COMPUTE );

		rscTracker.FlushBarriers( cmdBuff );

		// NOTE: exec barrier only need stages bc they don't access resources
		VkMemoryBarrier2 executionBarrier[] = { {
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			    .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		} };

		u32 mipLevel = 0;
		u32 srcImg = depthIdx.slot;
		for( u32 i = 0; i < hHiZTarget->mipCount; ++i )
		{
			if( i > 0 )
			{
				mipLevel = i - 1;
				srcImg = hizSrv.slot;
			}
			u32 dstImg = hizMipUavs[ i ].slot;

			u32 levelWidth = std::max( 1u, u32( hHiZTarget->width ) >> i );
			u32 levelHeight = std::max( 1u, u32( hHiZTarget->height ) >> i );

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

		rscTracker.UseImage( depthTarget, 
			{  HT_DEPTH_ATTACHMENT_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT }, 
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscTracker.UseImage( *hHiZTarget, 
			{ VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT }, VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.FlushBarriers( cmdBuff );
	}
};

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
	u32 pushDataSize
) {
	vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Draw Indexed Indirect Pass", {} );

	vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

	cmdBuff.CmdBindPipelineAndBindlessDesc( vkPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS );

	cmdBuff.CmdPushConstants( pPushData, pushDataSize );
	cmdBuff.CmdDrawIndexedIndirectCount( indexBuff, indexType, drawCmds, drawCount, maxDrawCount );
}

struct gpu_mesh_payload
{
	HVKBUF hMeshletBuffer;
	HVKBUF hVertexBuffer;
	HVKBUF hTriangleBuffer;
};

#include "ht_gfx_types.h"
#include "hell_pack.h"
#include "ht_math.h"

struct gpu_instance
{
	packed_trs transform;
	u16 meshIdx;
	u16 materialIdx;
};

struct desc_gpu_mesh
{
	vec3 minAabb;
	vec3 maxAabb;
	desc_hndl32 hMeshletBuffer;
	desc_hndl32 hVertexBuffer;
	desc_hndl32 hTriangleBuffer;
};

struct renderer_geometry
{
	std::vector<gpu_mesh_payload> gpuMeshes;
	std::vector<desc_gpu_mesh> gpuMeshDescs; // NOTE: matches 1:1 the above buffers

	HVKBUF hMeshTable; // NOTE: contains the above table
	HVKBUF hMaterialTable;

	HVKBUF hInstanceBuffer;

	HVKBUF hLights;

	std::vector<HVKIMG> hTextures;

	desc_hndl32 meshTableDesc;
	desc_hndl32 materialTableDesc;

	desc_hndl32 instBuffDesc;
	
	desc_hndl32 lightsDesc;

	std::vector<desc_hndl32> texuresDesc;

	//// NOTE: we never deallocate samplers 
	//std::vector<VkSampler> samplers;
	//std::vector<desc_hndl32> hSamplers;
};

struct virtual_frame
{
	//vk_gpu_timer gpuTimer;
	HVKCB hCmdBuff;
	VkSemaphore                 canGetImgSema;
	HVKBUF	                    hViewData;
	desc_hndl32                 viewDataIdx;
	u32                         fifIdx; // NOTE: for debug

	inline void Init( vk_context& vkCtx, u64 sizeInBytes, u32 fifIdx )
	{
		constexpr VkBufferUsageFlags usg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		fixed_string<32> name = { "Buff_VirtualFrame_ViewBuff{}", fifIdx };

		canGetImgSema = vkCtx.CreateBinarySemaphore();
		hViewData = vkCtx.CreateBuffer( { .name = std::data( name ), .usageFlags = usg, .sizeInBytes = sizeInBytes, 
			.usage = buffer_usage::HOST_VISIBLE } );
		viewDataIdx = vkCtx.AllocDescriptor( vk_descriptor_info{ *hViewData } );
		hCmdBuff = vkCtx.AllocateCmdPoolAndBuff( vk_queue_t::GFX );
	}
};

struct render_context final : renderer_interface
{
	alignas( 8 ) vk_renderer_config             config = { 
		.renderWidth = SCREEN_WIDTH, .rednerHeight = SCREEN_HEIGHT };

	imgui_pass									imguiPass;
	debug_draw_passes							dbgPass;
	culling_pass								cullingPass;
	tone_mapping_pass							tonemapPass;
	depth_pyramid_pass							hizbPass;

	vk_rsc_state_tracker						rscSyncState;

	fixed_vector<virtual_frame, MAX_FIF>        vrtFrames;

	std::unique_ptr<vk_context>                 pVkCtx;

	HVKIMG										hColorTarget;
	HVKIMG										hDepthTarget;

	// TODO: move to appropriate technique/context
	VkPipeline									gfxZPrepass;
	VkPipeline									gfxPipeline;
	VkPipeline									gfxMeshletPipeline;
	VkPipeline									gfxMergedPipeline;

	u64											vFrameIdx = 0;

	desc_hndl32									colSrv;
	desc_hndl32									depthSrv;

	const u8									framesInFlight = 2;
	
	VkSampler									pbrSampler;
	desc_hndl32									pbrSamplerIdx;

	// TODO: will disappear when we have all the vbuffer, gbuffer and so forth passes
	void InitGlobalResources( VkFormat desiredDepthFormat, VkFormat desiredColorFormat, u16 width, u16 height );

	virtual void InitBackend( uintptr_t hInst, uintptr_t hWnd ) override;
	//virtual void UploadAsync( const hellpack_view& hellpackView ) override;
	virtual void HostFrames( const frame_data& frameData, gpu_data& gpuData ) override;
};

std::unique_ptr<renderer_interface> MakeRenderer()
{
	return std::make_unique<render_context>();
}

void render_context::InitGlobalResources( VkFormat desiredDepthFormat, VkFormat desiredColorFormat, u16 width, u16 height )
{
	if( !IsValidHandle( hDepthTarget ) )
	{
		constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_info info = {
			.name = "Img_DepthTarget",
			.format = desiredDepthFormat,
			.usg = usgFlags,
			.width = width,
			.height = height,
			.layerCount = 1,
			.mipCount = 1,
		};
		hDepthTarget = pVkCtx->CreateImage( info );

		depthSrv = pVkCtx->AllocDescriptor( vk_descriptor_info{ hDepthTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		rscSyncState.UseImage( *hDepthTarget, {}, VK_IMAGE_LAYOUT_UNDEFINED );
	}
	if( !IsValidHandle( hColorTarget ) )
	{
		constexpr VkImageUsageFlags usgFlags =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image_info info = {
			.name = "Img_ColorTarget",
			.format = desiredColorFormat,
			.usg = usgFlags,
			.width = width,
			.height = height,
			.layerCount = 1,
			.mipCount = 1,
		};
		hColorTarget = pVkCtx->CreateImage( info );

		colSrv = pVkCtx->AllocDescriptor( vk_descriptor_info{ hColorTarget->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		VkSamplerCreateInfo samplerCreateInfo = { 
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.maxAnisotropy = 1.0f,
			.minLod = 0,
			.maxLod = VK_LOD_CLAMP_NONE,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};

		pbrSampler = pVkCtx->CreateSampler( samplerCreateInfo );
		pbrSamplerIdx = pVkCtx->AllocDescriptor( vk_descriptor_info{ pbrSampler } );

		rscSyncState.UseImage( *hColorTarget, {}, VK_IMAGE_LAYOUT_UNDEFINED );
	}
}

void render_context::InitBackend( uintptr_t hInst, uintptr_t hWnd )
{
	pVkCtx = std::make_unique<vk_context>( VkMakeContext( hInst, hWnd, config ) );

	{
		unique_shader_ptr vtx = pVkCtx->CreateShaderFromSpirv( SysReadFile( "../Shaders/v_z_prepass.vert.spv" ) );

		vk_gfx_shader_stage shaderStages[] = { *vtx };
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		gfxZPrepass = pVkCtx->CreateGfxPipeline( shaderStages, dynamicStates, 0, 0, config.desiredDepthFormat, DEFAULT_PSO );
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
	cullingPass.Init( *pVkCtx );
	tonemapPass.Init( *pVkCtx );
	{
		unique_shader_ptr vtx = pVkCtx->CreateShaderFromSpirv( SysReadFile( "../Shaders/vtx_merged.vert.spv" ) );
		unique_shader_ptr frag = pVkCtx->CreateShaderFromSpirv( SysReadFile( "../Shaders/pbr.frag.spv" ) );
		
		vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		gfxMergedPipeline = pVkCtx->CreateGfxPipeline( 
			shaderStages, dynamicStates, &config.desiredColorFormat, 1, config.desiredDepthFormat, DEFAULT_PSO );
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
	hizbPass.Init( *pVkCtx );

	//rndCtx.dbgCtx.Init( *rndCtx.pDevice, rndCtx.globalLayout, renderCfg );
	//rndCtx.dbgCtx.InitData( *rndCtx.pDevice, 1 * KB, 1 * KB );

	imguiPass = MakeImguiPass( *pVkCtx, pVkCtx->scConfig.format );

	//vrtFrames.resize( framesInFlight );
}

/*
void render_context::UploadAsync( const hellpack_view& hellpackView )
{
	vk_buffer stagingBuff = pVkCtx->CreateBuffer( {
		.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sizeInBytes = hellpackView.sizeInBytes, // NOTE: this includes the header and entry tables too WTF BRO ?!
		.usage = buffer_usage::STAGING
	} );

	std::memcpy( stagingBuff.hostVisible, hellpackView.base, hellpackView.sizeInBytes );


	vk_cb_hndl32 hCopyCmdBuff = pVkCtx->AllocateCmdPoolAndBuff( vk_queue_t::COPY );
	vk_command_buffer copyCmdBuff = { pVkCtx->GetCmdBuff( hCopyCmdBuff ), VK_NULL_HANDLE, VK_NULL_HANDLE };

	{
		auto[ basePtr, size ] = hellpackView.Bytes( hellpack_entry_type::INST );
		VkBufferCopy copyRegion = { .srcOffset = basePtr - hellpackView.base, .dstOffset = 0, .size = size };
		copyCmdBuff.CmdCopyBuffer( stagingBuff, instDescBuff, copyRegion );
	}

	{
		auto[ basePtr, size ] = hellpackView.Bytes( hellpack_entry_type::MLET );
		VkBufferCopy copyRegion = { .srcOffset = basePtr - hellpackView.base, .dstOffset = 0, .size = size };
		copyCmdBuff.CmdCopyBuffer( stagingBuff, instDescBuff, copyRegion );
	}

	{
		auto[ basePtr, size ] = hellpackView.Bytes( hellpack_entry_type::VTX );
		VkBufferCopy copyRegion = { .srcOffset = basePtr - hellpackView.base, .dstOffset = 0, .size = size };
		copyCmdBuff.CmdCopyBuffer( stagingBuff, instDescBuff, copyRegion );
	}

	{
		auto[ basePtr, size ] = hellpackView.Bytes( hellpack_entry_type::TRI );
		VkBufferCopy copyRegion = { .srcOffset = basePtr - hellpackView.base, .dstOffset = 0, .size = size };
		copyCmdBuff.CmdCopyBuffer( stagingBuff, instDescBuff, copyRegion );
	}

	pVkCtx->copyQueue.submitionCount++;
	pVkCtx->QueueSubmitToTimeline( pVkCtx->copyQueue, {}, {}, {}, copyCmdBuff.hndl );
}
*/

// TODO: use a queue timeline or smth to check if we can get a cmd buffer instead of vFrameIdx
void render_context::HostFrames( const frame_data& frameData, gpu_data& gpuData )
{
	const u64 currentFrameIdx = vFrameIdx++;
	const u64 currentFrameInFlightIdx = currentFrameIdx % framesInFlight;

	VkResult timelineWaitResult = pVkCtx->TimelineTryWaitFor( pVkCtx->gpuFrameTimeline, framesInFlight, UINT64_MAX );
	HT_ASSERT( timelineWaitResult < VK_TIMEOUT );

	[[unlikely]]
	if( currentFrameIdx < framesInFlight )
	{
		vrtFrames.push_back( {} );
		std::rbegin( vrtFrames )->Init( *pVkCtx, std::size( frameData.views ) * sizeof( view_data ), currentFrameInFlightIdx );
	}

	const virtual_frame& thisVFrame = vrtFrames[ currentFrameInFlightIdx ];

	HT_ASSERT( thisVFrame.hViewData->sizeInBytes == BYTE_COUNT( frameData.views ) );
	std::memcpy( thisVFrame.hViewData->hostVisible, std::data( frameData.views ), BYTE_COUNT( frameData.views ) );

	pVkCtx->FlushDeletionQueues( currentFrameIdx );

	vk_command_buffer thisFrameCmdBuffer = { thisVFrame.hCmdBuff->buff, pVkCtx->globalPipelineLayout, pVkCtx->descSet };

	static bool initResources = false;
	if( !initResources )
	{
		pVkCtx->CreateSwapchin();

		InitGlobalResources( config.desiredDepthFormat, config.desiredColorFormat, 
			config.renderWidth, config.rednerHeight );
		imguiPass.CreateUploadFontAtlasSync( *pVkCtx, thisFrameCmdBuffer, currentFrameIdx );
		//VkUploadResources( *vk.pDc, stagingManager, thisFrameCmdBuffer, entities, currentFrameIdx );

		u32 instCount = 1; //instDescBuff.sizeInBytes / sizeof( instance_desc );
		u32 mletCount = 1; //meshletBuff.sizeInBytes / sizeof( meshlet );
		cullingPass.InitSceneDependentData( *pVkCtx, instCount, mletCount * instCount );

		//dbgCtx.UploadDebugGeometry();

		rscSyncState.UseBuffer( *tonemapPass.hAverageLuminanceBuffer, 
			{ VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT} );

		thisFrameCmdBuffer.CmdFillVkBuffer( *tonemapPass.hAverageLuminanceBuffer, 0u );

		rscSyncState.UseBuffer( *tonemapPass.hAverageLuminanceBuffer, 
			{ HT_SHADER_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT} );

		rscSyncState.FlushBarriers( thisFrameCmdBuffer );

		rscSyncState.UseImage( *hizbPass.hHiZTarget, {}, VK_IMAGE_LAYOUT_UNDEFINED );

		initResources = true;
	}

	pVkCtx->FlushPendingDescriptorUpdates();

	u32 scImgIdx = pVkCtx->AcquireNextSwapchainImageBlocking( thisVFrame.canGetImgSema );
	const vk_swapchain_image& scImg = pVkCtx->scImgs[ scImgIdx ];

	const vk_image& depthTarget = *hDepthTarget;
	const vk_image& colorTarget = *hColorTarget;

	auto depthWrite = VkMakeAttachemntInfo( depthTarget.view, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto depthRead = VkMakeAttachemntInfo( depthTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorWrite = VkMakeAttachemntInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorRead = VkMakeAttachemntInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );

	VkViewport viewport = VkGetViewport( colorTarget.width, colorTarget.height );
	VkRect2D scissor = VkGetScissor( colorTarget.width, colorTarget.height );

	u32 instCount = ( u32 ) instDescBuff.sizeInBytes / sizeof( instance_desc );
	u32 mletCount = ( u32 ) meshletBuff.sizeInBytes / sizeof( meshlet );
	u32 meshletUpperBound = instCount * mletCount;

	DirectX::XMMATRIX t = DirectX::XMMatrixMultiply( 
		DirectX::XMMatrixScaling( 180.0f, 100.0f, 60.0f ), DirectX::XMMatrixTranslation( 20.0f, -10.0f, -60.0f ) );
	DirectX::XMFLOAT4X4A debugOcclusionWallTransf;
	DirectX::XMStoreFloat4x4A( &debugOcclusionWallTransf, t );

	DirectX::XMUINT2 colorTargetSize = { colorTarget.width, colorTarget.height };

	//VkResetGpuTimer( thisVFrame.cmdBuff, thisVFrame.gpuTimer );

	{
		//vk_time_section timePipeline = { thisVFrame.cmdBuff, thisVFrame.gpuTimer.queryPool, 0 };
		cullingPass.Execute( thisFrameCmdBuffer, rscSyncState, *hizbPass.hHiZTarget, instCount, 
			thisVFrame.viewDataIdx, hizbPass.hizSrv, hizbPass.quadMinSamplerIdx, false );

		rscSyncState.UseImage( depthTarget, 
			{ VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, HT_FRAGMENT_TESTS_STAGE }, 
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscSyncState.UseImage( colorTarget, 
			{ VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT }, 
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );

		rscSyncState.FlushBarriers( thisFrameCmdBuffer );

		struct {
			u64 vtxAddr, transfAddr, compactedArgsAddr, mtrlsAddr, lightsAddr; u32 camIdx, samplerIdx;
		} shadingPush = { 
				.vtxAddr = globVertexBuff.devicePointer, 
				.transfAddr = instDescBuff.devicePointer, 
				.compactedArgsAddr = cullingPass.hCompactedDrawArgs->devicePointer,
				.mtrlsAddr = materialsBuff.devicePointer, 
				.lightsAddr = lightsBuff.devicePointer,
				.camIdx = thisVFrame.viewDataIdx.slot,
				.samplerIdx = pbrSamplerIdx.slot
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
			gfxMergedPipeline,
			colorPassInfo,
			indexBuff,
			VK_INDEX_TYPE_UINT32,
			*cullingPass.hDrawCmds,
			*cullingPass.hDrawCount,
			meshletUpperBound,
			&shadingPush,
			sizeof(shadingPush)
		);

		//vk_rendering_info zDgbInfo = {
		//	.viewport = viewport,
		//	.scissor = scissor,
		//	.colorAttachments = {},
		//	.pDepthAttachment = &depthRead
		//};
		//dbgCtx.DrawCPU( thisFrameCmdBuffer, zDgbInfo, "Draw Occluder-Depth", debug_draw_type::TRIANGLE, 
		//					   thisVFrame.pViewData->devicePointer, 0, debugOcclusionWallTransf, 0 );

		hizbPass.Execute( thisFrameCmdBuffer, rscSyncState, depthTarget, depthSrv );

		rscSyncState.UseBuffer( *cullingPass.hDrawCount, 
			{ VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT } );
		rscSyncState.UseBuffer( *cullingPass.hAtomicWgCounter, 
			{ VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT } );

		rscSyncState.FlushBarriers( thisFrameCmdBuffer );

		cullingPass.Execute( thisFrameCmdBuffer, rscSyncState, *hizbPass.hHiZTarget,
			instCount, thisVFrame.viewDataIdx, hizbPass.hizSrv, hizbPass.quadMinSamplerIdx, true );

		rscSyncState.UseImage( depthTarget, 
			{ VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT }, 
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscSyncState.FlushBarriers( thisFrameCmdBuffer );

		colorPassInfo.pDepthAttachment = &depthRead;
		attInfos[ 0 ] = colorRead;
		DrawIndexedIndirectMerged(
			thisFrameCmdBuffer,
			gfxMergedPipeline,
			colorPassInfo,
			indexBuff,
			VK_INDEX_TYPE_UINT32,
			*cullingPass.hDrawCmds,
			*cullingPass.hDrawCount,
			meshletUpperBound,
			&shadingPush,
			sizeof(shadingPush)
		);

		hizbPass.Execute( thisFrameCmdBuffer, rscSyncState, depthTarget, depthSrv );

		//VkRenderingAttachmentInfo attInfosDbg[] = { colorRead };
		//vk_rendering_info colDgbInfo = {
		//	.viewport = viewport,
		//	.scissor = scissor,
		//	.colorAttachments = attInfosDbg,
		//	.pDepthAttachment = 0
		//};
		//dbgCtx.DrawCPU( thisFrameCmdBuffer, colDgbInfo, "Draw Occluder-Color", debug_draw_type::TRIANGLE, 
		//					   thisVFrame.pViewData->devicePointer, 1, debugOcclusionWallTransf, cyan );

		if( frameData.freezeMainView )
		{
			//VkRenderingAttachmentInfo attInfosDbg[] = { colorRead };
			//vk_rendering_info colDgbInfo = {
			//	.viewport = viewport,
			//	.scissor = scissor,
			//	.colorAttachments = attInfosDbg,
			//	.pDepthAttachment = 0
			//};
			//dbgCtx.DrawCPU( thisFrameCmdBuffer, colDgbInfo, "Draw Frustum", debug_draw_type::LINE, 
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

		thisFrameCmdBuffer.CmdFillVkBuffer( *tonemapPass.hLuminanceHistogramBuffer, 0u );
		thisFrameCmdBuffer.CmdFillVkBuffer( *tonemapPass.hAtomicWgCounterBuff, 0u );

		rscSyncState.UseBuffer( *tonemapPass.hLuminanceHistogramBuffer, 
			{ HT_SHADER_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		rscSyncState.UseBuffer( *tonemapPass.hAtomicWgCounterBuff, 
			{ HT_SHADER_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		rscSyncState.UseImage( colorTarget, 
			{ VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT }, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
		rscSyncState.FlushBarriers( thisFrameCmdBuffer );

		tonemapPass.AverageLuminancePass( thisFrameCmdBuffer, frameData.elapsedSeconds, colSrv, colorTargetSize );

		// NOTE: we need an exec dependency between AcquireNextSwapchainImageBlocking and the Tonemapping pass write
		constexpr VkPipelineStageFlags2 execDep = 
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		rscSyncState.UseImage( scImg.img, { 0, execDep }, VK_IMAGE_LAYOUT_UNDEFINED );
		rscSyncState.UseImage( scImg.img, { VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT }, 
			VK_IMAGE_LAYOUT_GENERAL );

		rscSyncState.UseBuffer( *tonemapPass.hAverageLuminanceBuffer, 
			{ VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		rscSyncState.FlushBarriers( thisFrameCmdBuffer );
		
		HT_ASSERT( ( colorTarget.width == scImg.img.width ) && ( colorTarget.height == scImg.img.height ) );

		tonemapPass.TonemappingGammaPass( thisFrameCmdBuffer, colSrv, scImg.writeDescIdx, colorTargetSize );

		rscSyncState.UseImage( colorTarget, { 0, 0 }, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscSyncState.UseImage( scImg.img, 
			{ HT_COLOR_ATTACHMENT_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT },
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscSyncState.FlushBarriers( thisFrameCmdBuffer );

		imguiPass.DrawUiPass( *pVkCtx, thisFrameCmdBuffer.hndl, scImg.img, currentFrameIdx, currentFrameInFlightIdx );

		rscSyncState.UseImage( scImg.img, { 0, 0 }, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
		rscSyncState.FlushBarriers( thisFrameCmdBuffer );

		// NOTE: remove sc image to avoid handling this logic inside the tracker 
		rscSyncState.StopTrackingResource( ( u64 ) scImg.img.hndl );
	}

	//gpuData.timeMs = VkCmdReadGpuTimeInMs( thisVFrame.cmdBuff, thisVFrame.gpuTimer );
	thisFrameCmdBuffer.CmdEndCmbBuffer();

	// NOTE: with all these cool stage masks we can only let the gpu run until it need the sc image THEN wait
	VkSemaphoreSubmitInfo waitScImgAcquire[] = { {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = thisVFrame.canGetImgSema,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	} };
	VkSemaphoreSubmitInfo signalRenderFinished[] = { {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = scImg.canPresentSema,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
	} };
	
	pVkCtx->gpuFrameTimeline.submitsIssuedCount++;
	pVkCtx->QueueSubmitToTimeline( 
		pVkCtx->gfxQueue, pVkCtx->gpuFrameTimeline, waitScImgAcquire, signalRenderFinished, thisFrameCmdBuffer.hndl );
	pVkCtx->QueuePresent( pVkCtx->gfxQueue, scImgIdx );
}
