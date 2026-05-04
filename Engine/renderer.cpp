#define VK_NO_PROTOTYPES
#define __VK // NOTE: used to not include all the vk shit everywhere
#include <vulkan.h>

#include <Volk/volk.h>
#include <offsetAllocator.hpp>

#include <string.h>
#include <string_view>
#include <span>
#include <format>
#include <memory>

#include "ht_core_types.h"
#include "ht_utils.h"
#include "engine_platform_common.h"

#include "vk_error.h"
#include "vk_resources.h"
#include "vk_sync.h"
#include "vk_pso.h"

#include "vk_context.h"

#include "ht_fixed_vector.h"
#include "ht_fixed_string.h"
#include "ht_slot_vector.h"

#include "engine_types.h"

#include "ht_geometry.h"
#include "ht_math.h"
#include <imgui.h>

// TODO: move sys_file to HtLib too ?
#include "ht_file.h"

#include <DirectXPackedVector.h>

namespace DXPacked = DirectX::PackedVector;


//====================CONSTS====================//
constexpr u64 MAX_FIF					= vk_renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED;
constexpr u64 MAX_TRIANGLES_IN_SCENE	= 10'000'000;
constexpr u64 MAX_VERTICES_IN_SCENE		= 5'000'000;
constexpr u64 MAX_MESHLETS_IN_SCENE		= 100'000;
constexpr u64 MAX_INSTANCES_IN_SCENE	= 10'000;
//==============================================//
// TODO: cvars
//====================CVARS=====================//

//==============================================//

//==============CONSTEXPR_SWITCH================//
constexpr VkCullModeFlags	HT_CULL_MODE	= IS_WORLD_RH ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
constexpr VkFrontFace		HT_FRONT_FACE	= IS_WORLD_RH ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
//==============================================//

#include "ht_renderer_types.h"

// NOTE: clear depth to 0 bc we	use RevZ
constexpr VkClearValue DEPTH_CLEAR_VAL = {};
constexpr VkClearValue RT_CLEAR_VAL = {};

enum render_target_op : u32
{
	LOAD		= VK_ATTACHMENT_LOAD_OP_LOAD,
	LOAD_CLEAR	= VK_ATTACHMENT_LOAD_OP_CLEAR,
	STORE		= VK_ATTACHMENT_STORE_OP_STORE
};


inline u32 GroupCount( u32 invocationCount, u32 workGroupSize )
{
	return ( invocationCount + workGroupSize - 1 ) / workGroupSize;
}

struct imgui_pass
{
	using index_t = u16;

	static_assert( sizeof( ImDrawVert ) == sizeof( imgui_vertex ) );
	static_assert( sizeof( ImDrawIdx ) == sizeof( index_t ) );

	static constexpr u64					DEFAULT_BUFF_SIZE = 16 * KB;

	fixed_vector<vk_buffer, MAX_FIF>		vtx;
	fixed_vector<vk_buffer, MAX_FIF>		idx;
	vk_image								fontAtlasImg;
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
			.name		= "Img_ImGuiFonts",
			.format		= VK_FORMAT_R8G8B8A8_UNORM,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= usgFlags,
			.width		= ( u16 ) width,
			.height		= ( u16 ) height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		u64 sizeInBytes = width * height * 4;

		vk_buffer stagingBuff = dc.CreateBuffer( {
			.usageFlags		= VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.sizeInBytes	= sizeInBytes,
			.usage			= buffer_usage::STAGING
		} );

		std::memcpy( stagingBuff.hostVisible, pixels, sizeInBytes );

		cmdBuff.CmdPipelineImageBarriers( VkMakeImageBarrier(
			fontAtlasImg, {}, { VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT },
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkFullResource( fontAtlasImg )
		) );
		cmdBuff.CmdCopyBufferToImageSubresource( stagingBuff, 0, fontAtlasImg, VkFullResourceLayers( fontAtlasImg ) );
		cmdBuff.CmdPipelineImageBarriers( VkMakeImageBarrier(
			fontAtlasImg, { VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT }, {}, 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VkFullResource( fontAtlasImg )
		) );

		dc.EnqueueResourceFree( vk_resc_deletion{ stagingBuff, frameIdx } );
	}

	// NOTE: it's mostly inspired by the official backend code
	void DrawUiPass(
		vk_context&		vkCtx,
		VkCommandBuffer cmdBuff,
		const vk_image& dstTarget,
		u64				frameIdx,
		u64				frameInFlightIdx
	) {
		HT_ASSERT( frameInFlightIdx < vtx.capacity() );
		HT_ASSERT( frameInFlightIdx < idx.capacity() );

		const ImDrawData* drawData = ImGui::GetDrawData();

		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		float fbWidth	= drawData->DisplaySize.x * drawData->FramebufferScale.x;
		float fbHeight	= drawData->DisplaySize.y * drawData->FramebufferScale.y;
		if( fbWidth <= 0.0f || fbHeight <= 0.0f ) return;

		// Textrues

		// NOTE: lazy init
		[[unlikely]] if( std::size( vtx ) <= frameInFlightIdx ) vtx.push_back( {} );
		[[unlikely]] if( std::size( idx ) <= frameInFlightIdx ) idx.push_back( {} );

		if( drawData->TotalVtxCount <= 0 ) return;
		
		u64 vtxTotalSizeInBytes = std::bit_ceil( drawData->TotalVtxCount * sizeof( imgui_vertex ) );
		u64 idxTotalSizeInBytes = std::bit_ceil( drawData->TotalIdxCount * sizeof( index_t ) );

		vk_buffer& refVtxBuff = vtx[ frameInFlightIdx ];
		vk_buffer& refIdxBuff = idx[ frameInFlightIdx ];

		if( refVtxBuff.sizeInBytes < vtxTotalSizeInBytes )
		{
			if( VK_NULL_HANDLE != refVtxBuff.hndl ) vkCtx.EnqueueResourceFree( vk_resc_deletion{ refVtxBuff, frameIdx } );
			refVtxBuff = vkCtx.CreateBuffer( { .usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				.sizeInBytes = vtxTotalSizeInBytes, .usage = buffer_usage::HOST_VISIBLE } );
		}

		if( refIdxBuff.sizeInBytes < idxTotalSizeInBytes )
		{
			if( VK_NULL_HANDLE != refIdxBuff.hndl ) vkCtx.EnqueueResourceFree( vk_resc_deletion{ refIdxBuff, frameIdx } );
			refIdxBuff = vkCtx.CreateBuffer( { .usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				.sizeInBytes = idxTotalSizeInBytes, .usage = buffer_usage::HOST_VISIBLE } );
		}

		const vk_buffer& vtxBuff = refVtxBuff;
		const vk_buffer& idxBuff = refIdxBuff;

		ImDrawVert* vtxDst	= ( ImDrawVert* ) vtxBuff.hostVisible;
		ImDrawIdx* idxDst	= ( ImDrawIdx* ) idxBuff.hostVisible;
		for( const ImDrawList* cmdList : drawData->CmdLists )
		{
			std::memcpy( vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof( imgui_vertex ) );
			std::memcpy( idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof( index_t ) );
			vtxDst += cmdList->VtxBuffer.Size;
			idxDst += cmdList->IdxBuffer.Size;
		}

		float2 scale		= { 2.0f / drawData->DisplaySize.x, 2.0f / drawData->DisplaySize.y };
		float2 move			= { -1.0f - drawData->DisplayPos.x * scale.x, -1.0f - drawData->DisplayPos.y * scale.y };
		float4 pushConst	= { scale.x, scale.y, move.x, move.y };

		vk_descriptor_info pushDescs[] = { vtxBuff, { fontSampler, fontAtlasImg.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } };

		vk_scoped_label label = { cmdBuff,"Draw Imgui Pass",{} };

		VkRect2D renderArea = VkGetScissor( dstTarget.width, dstTarget.height );

		// NOTE: we need a different viewport since this is drawn directly to the screen
		VkViewport uiViewport = { 0, 0, ( float ) dstTarget.width, ( float ) dstTarget.height, 0, 1.0f };
		vkCmdSetViewport( cmdBuff, 0, 1, &uiViewport );

		VkRenderingAttachmentInfo dstTargetAttachmentInfo = VkMakeAttachmentInfo(
			dstTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
		VkRenderingInfo renderInfo = {
			.sType					= VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea				= renderArea,
			.layerCount				= 1,
			.colorAttachmentCount	= 1,
			.pColorAttachments		= &dstTargetAttachmentInfo,
		};
		vk_scoped_renderpass renderPass = { cmdBuff, renderInfo };

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, descTemplate, pipelineLayout, 0, pushDescs );
		vkCmdPushConstants( cmdBuff, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( pushConst ), &pushConst );
		vkCmdBindIndexBuffer( cmdBuff, idxBuff.hndl, 0, VK_INDEX_TYPE_UINT16 );

		// (0,0) unless using multi-viewports
		float2 clipOff = { drawData->DisplayPos.x, drawData->DisplayPos.y };
		// (1,1) unless using retina display which are often (2,2)
		float2 clipScale = { drawData->FramebufferScale.x, drawData->FramebufferScale.y };

		u32 vtxOffset = 0, idxOffset = 0;
		for( u32 li = 0u; li < ( u32 ) drawData->CmdListsCount; ++li )
		{
			const ImDrawList* cmdList = drawData->CmdLists[ li ];
			for( u32 ci = 0u; ci < ( u32 ) cmdList->CmdBuffer.Size; ++ci )
			{
				const ImDrawCmd* pCmd = &cmdList->CmdBuffer[ ci ];
				// Project scissor/clipping rectangles into framebuffer space
				float2 clipMin = { ( pCmd->ClipRect.x - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.y - clipOff.y ) * clipScale.y };
				float2 clipMax = { ( pCmd->ClipRect.z - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.w - clipOff.y ) * clipScale.y };

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
		.sType						= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter					= VK_FILTER_LINEAR,
		.minFilter					= VK_FILTER_LINEAR,
		.mipmapMode					= VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxAnisotropy				= 1.0f,
		.minLod						= 0,
		.maxLod						= VK_LOD_CLAMP_NONE,
		.borderColor				= VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates	= VK_FALSE,
	};

	VkSampler fontSampler = dc.CreateSampler( samplerCreateInfo );

	VkDescriptorSetLayoutBinding descSetBindings[ 2 ] = {};
	descSetBindings[ 0 ].descriptorType		= VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descSetBindings[ 0 ].descriptorCount	= 1;
	descSetBindings[ 0 ].stageFlags			= VK_SHADER_STAGE_VERTEX_BIT;
	descSetBindings[ 0 ].binding			= 0;
	descSetBindings[ 1 ].descriptorType		= VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descSetBindings[ 1 ].descriptorCount	= 1;
	descSetBindings[ 1 ].stageFlags			= VK_SHADER_STAGE_FRAGMENT_BIT;
	descSetBindings[ 1 ].pImmutableSamplers = &fontSampler;
	descSetBindings[ 1 ].binding			= 1;

	VkDescriptorSetLayoutCreateInfo descSetInfo = { 
		.sType			= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags			= VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
		.bindingCount	= std::size( descSetBindings ),
		.pBindings		= descSetBindings
	};

	VkDescriptorSetLayout descSetLayout = {};
	VK_CHECK( vkCreateDescriptorSetLayout( dc.device, &descSetInfo, 0, &descSetLayout ) );

	VkPushConstantRange pushConst = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( float ) * 4 };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = { 
		.sType					= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount			= 1,
		.pSetLayouts			= &descSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges	= &pushConst
	};

	VkPipelineLayout pipelineLayout = {};
	VK_CHECK( vkCreatePipelineLayout( dc.device, &pipelineLayoutInfo, 0, &pipelineLayout ) );

	VkDescriptorUpdateTemplateEntry entries[ std::size( descSetBindings ) ] = {};
	for( u64 bi = 0; bi < std::size( descSetBindings ); ++bi )
	{
		VkDescriptorSetLayoutBinding bindingLayout = descSetBindings[ bi ];
		entries[ bi ] = {
			.dstBinding			= bindingLayout.binding,
			.descriptorCount	= 1,
			.descriptorType		= bindingLayout.descriptorType,
			.offset				= bi * sizeof( vk_descriptor_info ),
			.stride				= sizeof( vk_descriptor_info ),
		};
	}

	VkDescriptorUpdateTemplateCreateInfo templateInfo = { 
		.sType						= VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
		.descriptorUpdateEntryCount = std::size( entries ),
		.pDescriptorUpdateEntries	= std::data( entries ),
		.templateType				= VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR,
		.descriptorSetLayout		= descSetLayout,
		.pipelineBindPoint			= VK_PIPELINE_BIND_POINT_GRAPHICS,
		.pipelineLayout				= pipelineLayout
	};

	VkDescriptorUpdateTemplate descTemplate = {};
	VK_CHECK( vkCreateDescriptorUpdateTemplate( dc.device, &templateInfo, 0, &descTemplate ) );

	unique_shader_ptr vtx = dc.CreateShaderFromSpirv( 
		ReadFileBinary( "bin/SpirV/vertex_ImGuiVsMain.spirv" ) );
	unique_shader_ptr frag = dc.CreateShaderFromSpirv( 
		ReadFileBinary( "bin/SpirV/pixel_ImGuiPsMain.spirv" ) );

	vk_gfx_pso_config guiState = {
		.polyMode				= VK_POLYGON_MODE_FILL,
		.cullFlags				= VK_CULL_MODE_NONE,
		.frontFace				= VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.primTopology			= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.srcColorBlendFactor	= VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp			= VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor	= VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp			= VK_BLEND_OP_ADD,
		.depthWrite				= false,
		.depthTestEnable		= false,
		.blendCol				= true
	};

	vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipeline pipeline = dc.CreateGfxPipeline(
		shaderStages, dynamicStates, &colDstFormat, 1, VK_FORMAT_UNDEFINED, guiState, pipelineLayout );

	return {
		.fontSampler	= fontSampler,
		.descSetLayout	= descSetLayout,
		.pipelineLayout = pipelineLayout,
		.descTemplate	= descTemplate,
		.pipeline		= pipeline
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
		default: break;
	}
	return "UNKNOWN";
}

struct debug_draw_passes
{
	// TODO: where to place these ?
	static constexpr box_vertices			unitCube	= GenerateDbgBoxFromBounds( BOX_MIN, BOX_MAX );
	static constexpr box_wireframe_indices	lineVtxBuff = GenerateBoxWireframeIndices();
	//constexpr box_triangle_indices trisVtxBuff = BoxVerticesAsTriangles( unitCube );

	ht_stretchybuff<dbg_aabb_instance> cpuInstView = {};

	vk_buffer	vtxBuff;
	vk_buffer	idxBuff;
	vk_buffer	gpuInstBuff;
	vk_buffer	gpuInstCountBuff;
	vk_buffer	cpuInstBuff;

	vk_buffer	drawCmdsBuff;
	vk_buffer	drawCountBuff;

	VkPipeline	drawAsLines;
	VkPipeline	drawAsTriangles;
	// NOTE: it's easier to separate the recording of instances and the submission of draws
	VkPipeline	compRecordDbgDraw;

	VkPipeline	compLambertianClay;

	desc_hndl32	gpuInstBuffIdx;
	desc_hndl32	gpuInstCountBuffIdx;

	void Init( vk_context& dc, vk_renderer_config& rndCfg )
	{
		unique_shader_ptr vtx = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/vertex_DbgBoxVsMain.spirv" ) );
		unique_shader_ptr frag = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/pixel_ColPassPsMain.spirv" ) );

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		vk_gfx_shader_stage gfxStages[] = { *vtx, *frag };

		{
			vk_gfx_pso_config lineDrawPipelineState = {
				.polyMode			= VK_POLYGON_MODE_LINE,
				.cullFlags			= HT_CULL_MODE,
				.frontFace			= HT_FRONT_FACE,
				.primTopology		= VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
				.depthWrite			= VK_FALSE,
				.depthTestEnable	= VK_FALSE,
				.blendCol			= VK_FALSE,
			};

			drawAsLines = dc.CreateGfxPipeline( gfxStages, dynamicStates, &rndCfg.desiredColorFormat,
				1, VK_FORMAT_UNDEFINED, lineDrawPipelineState );
		}

		if constexpr( false )
		{
			vk_gfx_pso_config triDrawPipelineState = {
				.polyMode			= VK_POLYGON_MODE_FILL,
				.cullFlags			= VK_CULL_MODE_NONE,
				.frontFace			= VK_FRONT_FACE_COUNTER_CLOCKWISE,
				.primTopology		= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				.depthWrite			= VK_TRUE,
				.depthTestEnable	= VK_TRUE,
				.blendCol			= VK_TRUE
			};

			drawAsTriangles = dc.CreateGfxPipeline( gfxStages, dynamicStates, &rndCfg.desiredColorFormat,
				1, rndCfg.desiredDepthFormat, triDrawPipelineState );
		}

		unique_shader_ptr lambert = dc.CreateShaderFromSpirv(
			ReadFileBinary( "bin/SpirV/compute_LambertianClayCsMain.spirv" ) );
		compLambertianClay = dc.CreateComputePipeline( *lambert );

		unique_shader_ptr recordDbgDraw = dc.CreateShaderFromSpirv(
			ReadFileBinary( "bin/SpirV/compute_RecordDbgDrawCsMain.spirv" ) );
		compRecordDbgDraw = dc.CreateComputePipeline( *recordDbgDraw );

		constexpr VkBufferUsageFlags usgFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		drawCountBuff = dc.CreateBuffer( {
			.name			= "Buff_DbgDrawCount",
			.usageFlags		= usgFlags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes	= 1 * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY
		} );

		drawCmdsBuff = dc.CreateBuffer( {
			.name			= "Buff_DbgDrawCount",
			.usageFlags		= usgFlags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
			.sizeInBytes	= MAX_INSTANCES_IN_SCENE * sizeof( draw_instanced_indexed_indirect ),
			.usage			= buffer_usage::GPU_ONLY
		} );

		gpuInstBuff = dc.CreateBuffer( {
			.name			= "Buff_DbgGpuInst",
			.usageFlags		= usgFlags,
			.sizeInBytes	= MAX_INSTANCES_IN_SCENE * sizeof( dbg_aabb_instance ),
			.usage			= buffer_usage::GPU_ONLY
		} );
		gpuInstBuffIdx = dc.AllocDescriptorIdx( gpuInstBuff );

		gpuInstCountBuff = dc.CreateBuffer( {
			.name			= "Buff_DbgGpuInstCount",
			.usageFlags		= usgFlags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes	= 1 * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY
		} );
		gpuInstCountBuffIdx = dc.AllocDescriptorIdx( gpuInstCountBuff );

		cpuInstBuff = dc.CreateBuffer( {
			.name			= "Buff_DbgCpuInst",
			.usageFlags		= usgFlags,
			.sizeInBytes	= MAX_INSTANCES_IN_SCENE * sizeof( dbg_aabb_instance ),
			.usage			= buffer_usage::HOST_VISIBLE
		} );
		cpuInstView = HtNewStretchyBuffFromMem<dbg_aabb_instance>( cpuInstBuff.hostVisible, cpuInstBuff.sizeInBytes );
	}

	void InitAndUploadDebugGeometry( vk_context& dc )
	{
		// NOTE: host vis to simplify uploads
		// NOTE: arbitrary sizes chosen
		vtxBuff = dc.CreateBuffer( {
			.name			= "Buff_DbgDrawCount",
			.usageFlags		=  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sizeInBytes	= 500 * sizeof( dbg_vertex ),
			.usage			= buffer_usage::HOST_VISIBLE
		} );

		idxBuff = dc.CreateBuffer( {
			.name			= "Buff_DbgDrawCount",
			.usageFlags		= VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sizeInBytes	= 2000 * sizeof( dbg_index_t ),
			.usage			= buffer_usage::HOST_VISIBLE
		} );

		std::memcpy( vtxBuff.hostVisible, std::data( unitCube ), BYTE_COUNT( unitCube ) );
		std::memcpy( idxBuff.hostVisible, std::data( lineVtxBuff ), BYTE_COUNT( lineVtxBuff ) );
	}

	inline void ResetDrawCounters( vk_command_buffer& cmdBuff, vk_rsc_state_tracker& rscTracker )
	{
		rscTracker.UseBuffer( drawCountBuff, TRANSFER_WRITE );
		rscTracker.UseBuffer( gpuInstCountBuff, TRANSFER_WRITE );

		rscTracker.FlushBarriers( cmdBuff );

		cmdBuff.CmdFillVkBuffer( drawCountBuff, 0u );
		cmdBuff.CmdFillVkBuffer( gpuInstCountBuff, 0u );
	}

	void DrawWireframesGPU(
		vk_command_buffer&      		cmdBuff,
		vk_rsc_state_tracker&			rscTracker,
		const vk_image&  				colorTarget,
		desc_hndl32						camIdx
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Dbg_DrawWireframesGPU", {} );

		rscTracker.UseBuffer( drawCountBuff, COMPUTE_WRITE );
		rscTracker.UseBuffer( drawCmdsBuff, COMPUTE_WRITE );
		rscTracker.UseBuffer( gpuInstCountBuff, COMPUTE_READ );
		rscTracker.FlushBarriers( cmdBuff );

		{
			cmdBuff.CmdBindPipelineAndBindlessDesc( compRecordDbgDraw, VK_PIPELINE_BIND_POINT_COMPUTE );
			record_dbg_draw_params pushBlock = {
				.gpuInstCountAddr	= gpuInstCountBuff.devicePointer,
				.dbgDrawCmdsAddr	= drawCmdsBuff.devicePointer,
				.dbgDrawCountAddr	= drawCountBuff.devicePointer,
				.indexCount			= ( u32 ) std::size( lineVtxBuff ),
				.firstIndex			= 0,
				.vertexOffset		= 0
			};
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDispatch( { 1, 1, 1 } );
		}

		rscTracker.UseBuffer( drawCmdsBuff, DRAW_INDIRECT_READ );
		rscTracker.UseBuffer( drawCountBuff, DRAW_INDIRECT_READ );
		rscTracker.UseBuffer( gpuInstBuff, { VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT } );
		rscTracker.UseImage( colorTarget,
			{ HT_COLOR_ATTACHMENT_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT },
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );

		rscTracker.FlushBarriers( cmdBuff );

		{
			VkRenderingAttachmentInfo attInfos[] = {
				VkMakeAttachmentInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} )
			};

			vk_rendering_info renderingInfo = {
				.viewport = VkCorrectedGetViewport( colorTarget.width, colorTarget.height ),
				.scissor = VkGetScissor( colorTarget.width, colorTarget.height ),
				.colorAttachments = attInfos,
				.pDepthAttachment = nullptr
			};

			vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

			cmdBuff.CmdBindPipelineAndBindlessDesc( drawAsLines, VK_PIPELINE_BIND_POINT_GRAPHICS );

			dbg_box_params pushBlock = {
				.instBuffAddr	= gpuInstBuff.devicePointer, // NOTE: it's GPU !!!!
				.vtxBuffAddr	= vtxBuff.devicePointer,
				.camIdx			= camIdx.slot
			};
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDrawIndexedIndirectCount<draw_instanced_indexed_indirect>(
				idxBuff, VK_INDEX_TYPE_UINT16, drawCmdsBuff, drawCountBuff );
		}
	}

	// TODO: don't hardcode
	void DrawWireframeCPU(
		vk_command_buffer&      		cmdBuff,
		vk_rsc_state_tracker&			rscTracker,
		const vk_image&  				colorTarget,
		desc_hndl32						camIdx
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Dbg_DrawWireframeCPU", {} );

		rscTracker.UseImage( colorTarget,
			{ HT_COLOR_ATTACHMENT_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT },
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );

		rscTracker.FlushBarriers( cmdBuff );

		VkRenderingAttachmentInfo attInfos[] = {
			VkMakeAttachmentInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} )
		};

		vk_rendering_info renderingInfo = {
			.viewport = VkCorrectedGetViewport( colorTarget.width, colorTarget.height ),
			.scissor = VkGetScissor( colorTarget.width, colorTarget.height ),
			.colorAttachments = attInfos,
			.pDepthAttachment = nullptr
		};

		vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

		cmdBuff.CmdBindPipelineAndBindlessDesc( drawAsLines, VK_PIPELINE_BIND_POINT_GRAPHICS );

		dbg_box_params pushBlock = {
			.instBuffAddr	= cpuInstBuff.devicePointer, // NOTE: it's CPU !!!!
			.vtxBuffAddr	= vtxBuff.devicePointer,
			.camIdx			= camIdx.slot
		};
		cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
		cmdBuff.CmdDrawIndexed( idxBuff, VK_INDEX_TYPE_UINT16, ( u32 ) std::size( lineVtxBuff ),
			1, 0, 0, 0  );
	}

	void DrawAsLamberitanClay(
		vk_command_buffer&        		cmdBuff,
		vk_rsc_state_tracker&			rscTracker,
		const vk_image&					vbuff,
		const vk_image&					dstImg,
		const lambertian_clay_params&	pushBlock
	) {
		HT_ASSERT( ( vbuff.width == dstImg.width ) && ( vbuff.height == dstImg.height ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Dbg_LambertianClayPass", {} );

		rscTracker.UseImage( dstImg, { VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT },
			VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.UseImage( vbuff,{ VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT },
			VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.FlushBarriers( cmdBuff );

		cmdBuff.CmdBindPipelineAndBindlessDesc( compLambertianClay, VK_PIPELINE_BIND_POINT_COMPUTE );

		cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );

		group_size groupSize = { 16, 16, 1 };
		u32x3 numWorkGrs = {
			GroupCount( dstImg.width, groupSize.x ),
			GroupCount( dstImg.height, groupSize.y ), 1
		};
		cmdBuff.CmdDispatch( numWorkGrs );
	}
};


using index_t = u8;

struct culling_pass_args
{
	const vk_buffer&		dbgGpuInstBuff;
	const vk_buffer&		dbgGpuInstCountBuff;
	const vk_image&			hiZTarget;
	u32						instCount;
	desc_hndl32				instBuffIdx;
	desc_hndl32				meshTableIdx;
	desc_hndl32				viewBuffIdx;
	u32						camIdx;
	desc_hndl32				hizDesc;
	desc_hndl32				samplerDesc;
	desc_hndl32				dbgGpuInstBuffIdx;
	desc_hndl32				dbgGpuInstCountBuffIdx;
};

struct culling_pass
{
	vk_buffer		instOccludedCache;
	vk_buffer		clusterOccludedCache;

	vk_buffer		visibleInstances;
	vk_buffer		visibleInstCounter;
	vk_buffer		visibleClusters;
	vk_buffer		visibleClustersCount;

	vk_buffer		dispatchIndirect;

	vk_buffer		drawCmds;
	vk_buffer		drawCount;

	VkPipeline		instCullPass;
	VkPipeline		instExpansionPass;
	VkPipeline		indirectDispatchPass;
	VkPipeline      clusterCullPipe;

	desc_hndl32		instOccludedCacheIdx;
	desc_hndl32		clusterOccludedCacheIdx;

	desc_hndl32		visibleInstIdx;
	desc_hndl32		visibleInstCounterIdx;
	desc_hndl32		visibleClustersIdx;
	desc_hndl32		visibleClustersCountIdx;

	desc_hndl32		dispatchIndirectIdx;

	desc_hndl32		drawCmdsIdx;
	desc_hndl32		drawCountIdx;


	void Init( vk_context& dc )
	{
		unique_shader_ptr instCull = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/compute_DrawCullCsMain.spirv" ) );
		instCullPass = dc.CreateComputePipeline( *instCull );

		unique_shader_ptr instExp = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/compute_ExpandDrawsCsMain.spirv" ) );
		instExpansionPass = dc.CreateComputePipeline( *instExp );

		unique_shader_ptr dispatcher = dc.CreateShaderFromSpirv(
			ReadFileBinary( "bin/SpirV/compute_IndirectDispatcherCsMain.spirv" ) );
		indirectDispatchPass = dc.CreateComputePipeline( *dispatcher );

		unique_shader_ptr clusterCull = dc.CreateShaderFromSpirv(
			ReadFileBinary( "bin/SpirV/compute_IssueMeshletDrawsCsMain.spirv" ) );
		clusterCullPipe = dc.CreateComputePipeline( *clusterCull );

		constexpr VkBufferUsageFlags usgFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		drawCount = dc.CreateBuffer( {
			.name			= "Buff_DrawCount",
			.usageFlags 	= usgFlags,
			.sizeInBytes 	= 1 * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY
		} );
		drawCountIdx = dc.AllocDescriptorIdx( drawCount );

		visibleInstCounter = dc.CreateBuffer( {
			.name			= "Buff_VisibleInstCounter",
			.usageFlags		= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
			| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sizeInBytes	= 1 * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY
		} ); 
		visibleInstCounterIdx = dc.AllocDescriptorIdx( visibleInstCounter );

		dispatchIndirect = dc.CreateBuffer( {
			.name = "Buff_DispatchIndirect",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT 
			| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sizeInBytes = 1 * sizeof( dispatch_command ),
			.usage = buffer_usage::GPU_ONLY 
		} );
		dispatchIndirectIdx = dc.AllocDescriptorIdx( dispatchIndirect );

		constexpr VkBufferUsageFlags usg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		// NOTE: these are hard capped
		clusterOccludedCache = dc.CreateBuffer( {
			.name			= "Buff_ClusterOccludedCache",
			.usageFlags		= usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes	= MAX_MESHLETS_IN_SCENE * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY } );
		clusterOccludedCacheIdx = dc.AllocDescriptorIdx( clusterOccludedCache );

		visibleClusters = dc.CreateBuffer( {
			.name			= "Buff_VisibleClusters",
			.usageFlags		= usg,
			.sizeInBytes	= MAX_MESHLETS_IN_SCENE * sizeof( visible_meshlet ),
			.usage			= buffer_usage::GPU_ONLY } );
		visibleClustersIdx = dc.AllocDescriptorIdx( visibleClusters );

		visibleClustersCount = dc.CreateBuffer( {
			.name			= "Buff_VisibleClustersCount",
			.usageFlags		= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
			| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sizeInBytes	= 1 * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY } );
		visibleClustersCountIdx = dc.AllocDescriptorIdx( visibleClustersCount );

		drawCmds = dc.CreateBuffer( {
			.name			= "Buff_DrawCmds",
			.usageFlags		= usg | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT ,
			.sizeInBytes	= MAX_MESHLETS_IN_SCENE * sizeof( draw_indexed_command ),
			.usage			= buffer_usage::GPU_ONLY } );
		drawCmdsIdx = dc.AllocDescriptorIdx( drawCmds );
	}

	void InitSceneDependentData( vk_context& dc, u32 instancesUpperBound )
	{
		constexpr VkBufferUsageFlags usg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		instOccludedCache = dc.CreateBuffer( {
			.name			= "Buff_InstanceOccludedCache",
			.usageFlags		= usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes	= instancesUpperBound * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY } );
		instOccludedCacheIdx = dc.AllocDescriptorIdx( instOccludedCache );

		visibleInstances = dc.CreateBuffer( {
			.name			= "Buff_VisibleInstArgs",
			.usageFlags		= usg,
			.sizeInBytes	= instancesUpperBound * sizeof( visible_instance ),
			.usage			= buffer_usage::GPU_ONLY } );
		visibleInstIdx = dc.AllocDescriptorIdx( visibleInstances );
	}

	void Execute(
		vk_command_buffer&			cmdBuff,
		vk_rsc_state_tracker&		rscTracker,
		const culling_pass_args&	args,
		bool						latePass
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Cull Pass",{} );

		if( !latePass )
		{
			rscTracker.UseBuffer( instOccludedCache, COMPUTE_READWRITE );
			rscTracker.UseBuffer( clusterOccludedCache, COMPUTE_READWRITE );
		}
		rscTracker.UseBuffer( drawCount, TRANSFER_WRITE );
		rscTracker.UseBuffer( visibleInstCounter, TRANSFER_WRITE );
		rscTracker.UseBuffer( visibleClustersCount, TRANSFER_WRITE );

		rscTracker.FlushBarriers( cmdBuff );

		cmdBuff.CmdFillVkBuffer( drawCount, 0u );
		cmdBuff.CmdFillVkBuffer( visibleInstCounter, 0u );
		cmdBuff.CmdFillVkBuffer( visibleClustersCount, 0u );

		if( !latePass )
		{
			cmdBuff.CmdFillVkBuffer( instOccludedCache, 0u );
			cmdBuff.CmdFillVkBuffer( clusterOccludedCache, 0u );
		}

		rscTracker.UseBuffer( drawCmds, COMPUTE_WRITE );
		rscTracker.UseBuffer( drawCount, COMPUTE_READWRITE );

		rscTracker.UseBuffer( dispatchIndirect, COMPUTE_WRITE );

		rscTracker.UseBuffer( visibleInstCounter, COMPUTE_WRITE );
		rscTracker.UseBuffer( visibleInstances, COMPUTE_WRITE );

		rscTracker.UseBuffer( visibleClusters, COMPUTE_WRITE );
		rscTracker.UseBuffer( visibleClustersCount, COMPUTE_WRITE );

		rscTracker.UseBuffer( args.dbgGpuInstBuff, COMPUTE_WRITE );
		rscTracker.UseBuffer( args.dbgGpuInstCountBuff, COMPUTE_WRITE );

		rscTracker.UseImage( args.hiZTarget, COMPUTE_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );

		rscTracker.FlushBarriers( cmdBuff );

		constexpr VkMemoryBarrier2 computeToComputeExecDependency[] = {
			VkMemoryBarrier2{
				.sType			= VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask	= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask	= VK_ACCESS_2_SHADER_WRITE_BIT,
				.dstStageMask	= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask	= HT_SHADER_ACCESS_READ_WRITE,
			},
		};
		constexpr VkMemoryBarrier2 computeToIndirectComputeExecDependency[] = {
			VkMemoryBarrier2{
				.sType			= VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask	= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask	= VK_ACCESS_2_SHADER_WRITE_BIT,
				.dstStageMask	= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
				.dstAccessMask	= HT_SHADER_ACCESS_READ_WRITE | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
			},
		};

		{
			culling_params pushBlock = {
				.instCount				= args.instCount,
				.visInstCacheIdx		= instOccludedCacheIdx.slot,
				.instDescIdx			= args.instBuffIdx.slot,
				.meshDescIdx			= args.meshTableIdx.slot,
				.viewBuffIdx			= args.viewBuffIdx.slot,
				.camIdx					= args.camIdx,
				.hizTexIdx				= args.hizDesc.slot,
				.hizSamplerIdx			= args.samplerDesc.slot,
				.visibleItemsCountIdx	= visibleInstCounterIdx.slot,
				.visibleItemsIdx		= visibleInstIdx.slot,
				.isLatePass				= latePass,

				.dbgInstCountIdx		= args.dbgGpuInstCountBuffIdx.slot,
				.dbgInstBuffIdx			= args.dbgGpuInstBuffIdx.slot
			};

			cmdBuff.CmdBindPipelineAndBindlessDesc( instCullPass, VK_PIPELINE_BIND_POINT_COMPUTE );
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDispatch( { GroupCount( args.instCount, 32 ), 1, 1 } );
		}
		cmdBuff.CmdPipelineMemoryBarriers( computeToComputeExecDependency );
		{
			indirect_dispatcher_params pushBlock = {
				.cullShaderWorkGrX	= 32,
				.dispatchCmdBuffIdx = dispatchIndirectIdx.slot,
				.counterBufferIdx	= visibleInstCounterIdx.slot
			};
			cmdBuff.CmdBindPipelineAndBindlessDesc( indirectDispatchPass, VK_PIPELINE_BIND_POINT_COMPUTE );
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDispatch( { 1, 1, 1 } );
		}
		cmdBuff.CmdPipelineMemoryBarriers( computeToIndirectComputeExecDependency );
		{
			draw_expansion_params pushBlock = {
				.workCounterIdxConst	= visibleInstCounterIdx.slot,
				.srcBufferIdx			= visibleInstIdx.slot,
				.visMltBufferIdx		= visibleClustersIdx.slot,
				.visMltCounterIdx		= visibleClustersCountIdx.slot
			};

			cmdBuff.CmdBindPipelineAndBindlessDesc( instExpansionPass, VK_PIPELINE_BIND_POINT_COMPUTE );
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			vkCmdDispatchIndirect( cmdBuff.hndl, dispatchIndirect.hndl, 0 );
		}
		cmdBuff.CmdPipelineMemoryBarriers( computeToComputeExecDependency );
		{
			indirect_dispatcher_params pushBlock = {
				.cullShaderWorkGrX	= 32,
				.dispatchCmdBuffIdx = dispatchIndirectIdx.slot,
				.counterBufferIdx	= visibleClustersCountIdx.slot
			};
			cmdBuff.CmdBindPipelineAndBindlessDesc( indirectDispatchPass, VK_PIPELINE_BIND_POINT_COMPUTE );
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDispatch( { 1, 1, 1 } );
		}
		cmdBuff.CmdPipelineMemoryBarriers( computeToIndirectComputeExecDependency );
		{
			meshlet_issue_draws_params pushBlock = {
				.visMltCountIdx		= visibleClustersCountIdx.slot,
				.srcBufferIdx		= visibleClustersIdx.slot,
				.drawCmdCounterIdx	= drawCountIdx.slot,
				.drawCmdsBuffIdx	= drawCmdsIdx.slot
			};
			cmdBuff.CmdBindPipelineAndBindlessDesc( clusterCullPipe, VK_PIPELINE_BIND_POINT_COMPUTE );
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			vkCmdDispatchIndirect( cmdBuff.hndl, dispatchIndirect.hndl, 0 );
		}
	}
};

struct tone_mapping_pass
{
	vk_buffer					averageLuminanceBuffer;
	vk_buffer					luminanceHistogramBuffer;
	vk_buffer					atomicWgCounterBuff;

	VkPipeline					compAvgLumPipe;
	VkPipeline					compTonemapPipe;

	desc_hndl32					avgLumIdx;
	desc_hndl32					atomicWgCounterIdx;
	desc_hndl32					lumHistoIdx;

	void Init( vk_context& dc )
	{
		unique_shader_ptr avgLum = dc.CreateShaderFromSpirv( 
			ReadFileBinary( "bin/SpirV/compute_AvgLuminanceCsMain.spirv" ) );
		compAvgLumPipe = dc.CreateComputePipeline( *avgLum );

		unique_shader_ptr toneMapper = dc.CreateShaderFromSpirv( 
			ReadFileBinary( "bin/SpirV/compute_TonemappingGammaCsMain.spirv" ) );
		compTonemapPipe = dc.CreateComputePipeline( *toneMapper );

		VkBufferUsageFlags usageFlags = 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		averageLuminanceBuffer = dc.CreateBuffer( {
			.name = "Buff_AvgLum",
			.usageFlags = usageFlags,
			.sizeInBytes = 1 * sizeof( float ),
			.usage = buffer_usage::GPU_ONLY } );
		avgLumIdx = dc.AllocDescriptorIdx( averageLuminanceBuffer );

		atomicWgCounterBuff = dc.CreateBuffer( {
			.name = "Buff_TonemappingAtomicWgCounter",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes = 1 * sizeof( u32 ),
			.usage = buffer_usage::GPU_ONLY } );
		atomicWgCounterIdx = dc.AllocDescriptorIdx( atomicWgCounterBuff );

		luminanceHistogramBuffer = dc.CreateBuffer( {
			.name = "Buff_LumHisto",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes = 4 * sizeof( u64 ),
			.usage = buffer_usage::GPU_ONLY } ); 
		lumHistoIdx = dc.AllocDescriptorIdx( luminanceHistogramBuffer );
	}

	void AverageLuminancePass( 
		vk_command_buffer&		cmdBuff,
		vk_rsc_state_tracker&	rscTracker,
		const vk_image&			colTarget,
		desc_hndl32				hdrColSrcDesc,
		float					dt
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Average Lum Pass", {} );

		cmdBuff.CmdFillVkBuffer( luminanceHistogramBuffer, 0u );
		cmdBuff.CmdFillVkBuffer( atomicWgCounterBuff, 0u );

		rscTracker.UseBuffer( luminanceHistogramBuffer, COMPUTE_READWRITE );
		rscTracker.UseBuffer( atomicWgCounterBuff, COMPUTE_READWRITE );
		rscTracker.UseImage( colTarget, COMPUTE_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
		rscTracker.FlushBarriers( cmdBuff );


		cmdBuff.CmdBindPipelineAndBindlessDesc( compAvgLumPipe, VK_PIPELINE_BIND_POINT_COMPUTE );

		// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
		avg_luminance_info avgLumInfo = {
			.minLogLum = -10.0f,
			.invLogLumRange = 1.0f / 12.0f,
			.dt = dt
		};

		group_size groupSize = { 16, 16, 1 };
		u32x3 numWorkGrs = {
			GroupCount( colTarget.width, groupSize.x ),
			GroupCount( colTarget.height, groupSize.y ), 1
		};
		struct push_const
		{
			avg_luminance_info  avgLumInfo;
			u32					hdrColSrcIdx;
			u32					lumHistoIdx;
			u32					atomicWorkGrCounterIdx;
			u32					avgLumIdx;
		} pushConst = { avgLumInfo, hdrColSrcDesc.slot, lumHistoIdx.slot, atomicWgCounterIdx.slot, avgLumIdx.slot };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		cmdBuff.CmdDispatch( numWorkGrs );
	}

	void TonemappingGammaPass(
		vk_command_buffer&		cmdBuff,
		vk_rsc_state_tracker&	rscTracker,
		const vk_image&			dstImg,
		desc_hndl32				hdrColDesc,
		desc_hndl32				sdrColDesc,
		DirectX::XMUINT2		hdrTrgSize
	) {
		HT_ASSERT( ( hdrTrgSize.x == dstImg.width ) && ( hdrTrgSize.y == dstImg.height ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Tonemapping Gamma Pass", {} );

		rscTracker.UseBuffer( averageLuminanceBuffer,
			{ VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } );
		rscTracker.UseImage( dstImg, { VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT },
			VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.FlushBarriers( cmdBuff );

		cmdBuff.CmdBindPipelineAndBindlessDesc( compTonemapPipe, VK_PIPELINE_BIND_POINT_COMPUTE );

		group_size groupSize = { 16, 16, 1 };
		u32x3 numWorkGrs = { GroupCount( hdrTrgSize.x, groupSize.x ), GroupCount( hdrTrgSize.y, groupSize.y ), 1 };
		struct push_const
		{
			u32 hdrColIdx;
			u32 sdrColIdx;
			u32 avgLumIdx;
		} pushConst = { hdrColDesc.slot, sdrColDesc.slot, avgLumIdx.slot };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		cmdBuff.CmdDispatch( numWorkGrs );
	}
};

struct depth_pyramid_pass
{
	VkPipeline		pipeline;
	vk_image		hiZTarget;
	VkImageView		hiZMipViews[ MAX_MIP_LEVELS ];

	VkSampler       quadMinSampler;

	desc_hndl32     hizSrv;

	desc_hndl32		hizMipUavs[ MAX_MIP_LEVELS ];
	desc_hndl32		quadMinSamplerIdx;

	void Init( vk_context& vkCtx )
	{
		unique_shader_ptr downsampler = vkCtx.CreateShaderFromSpirv( 
			ReadFileBinary( "bin/SpirV/compute_Pow2DownSamplerCsMain.spirv" ) );
		pipeline = vkCtx.CreateComputePipeline( *downsampler );

		u16 squareDim	= 512;
		u8 hiZMipCount	= GetImgMipCount( squareDim, squareDim, MAX_MIP_LEVELS );

		HT_ASSERT( MAX_MIP_LEVELS >= hiZMipCount );

		constexpr VkImageUsageFlags hiZUsg =
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		image_info hiZInfo = {
			.name		= "Img_HiZ",
			.format		= VK_FORMAT_R32_SFLOAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= hiZUsg,
			.width		= squareDim,
			.height		= squareDim,
			.layerCount = 1,
			.mipCount	= hiZMipCount
		};

		hiZTarget = vkCtx.CreateImage( hiZInfo );

		hizSrv = vkCtx.AllocDescriptorIdx( { hiZTarget.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		for( u32 i = 0; i < hiZTarget.mipCount; ++i )
		{
			hiZMipViews[ i ] = VkMakeImgView( vkCtx.device, hiZTarget.hndl, hiZInfo.format, i, 1,
				VK_IMAGE_VIEW_TYPE_2D, 0, hiZInfo.layerCount );
			hizMipUavs[ i ] = vkCtx.AllocDescriptorIdx( { hiZMipViews[ i ], VK_IMAGE_LAYOUT_GENERAL } );
		}

		VkSamplerReductionModeCreateInfo reduxInfo = { 
			.sType			= VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
			.reductionMode	= VK_SAMPLER_REDUCTION_MODE_MIN,
		};

		VkSamplerCreateInfo samplerCreateInfo = { 
			.sType						= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.pNext						= &reduxInfo,
			.magFilter					= VK_FILTER_LINEAR,
			.minFilter					= VK_FILTER_LINEAR,
			.mipmapMode					= VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.maxAnisotropy				= 1.0f,
			.minLod 					= 0,
			.maxLod						= VK_LOD_CLAMP_NONE,
			.borderColor				= VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates	= VK_FALSE,
		};

		quadMinSampler = vkCtx.CreateSampler( samplerCreateInfo );
		quadMinSamplerIdx = vkCtx.AllocDescriptorIdx( { quadMinSampler } );
	}

	void Execute( 
		vk_command_buffer&		cmdBuff,
		vk_rsc_state_tracker&	rscTracker,
		const vk_image&			depthTarget,
		desc_hndl32				depthIdx
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "HiZ Multi Pass", {} );

		rscTracker.UseImage( depthTarget, COMPUTE_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
		rscTracker.UseImage( hiZTarget, COMPUTE_WRITE, VK_IMAGE_LAYOUT_GENERAL );

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
		for( u32 i = 0; i < hiZTarget.mipCount; ++i )
		{
			if( i > 0 )
			{
				mipLevel = i - 1;
				srcImg = hizSrv.slot;
			}
			u32 dstImg = hizMipUavs[ i ].slot;

			u32 levelWidth = std::max( 1u, u32( hiZTarget.width ) >> i );
			u32 levelHeight = std::max( 1u, u32( hiZTarget.height ) >> i );

			float2 reduceData{ ( float ) levelWidth, ( float ) levelHeight };

			struct push_const
			{
				float2	reduce;
				u32 	samplerIdx;
				u32 	srcImgIdx;
				u32 	mipLevel;
				u32 	dstImgIdx;

				push_const(float2 r, u32 s, u32 src, u32 mip, u32 dst)
					: reduce(r), samplerIdx(s), srcImgIdx(src), mipLevel(mip), dstImgIdx(dst) {}
			};
			push_const pushConst{ float2{ (float) levelWidth, (float) levelHeight}, quadMinSamplerIdx.slot, srcImg, mipLevel, dstImg };
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
		rscTracker.UseImage( hiZTarget, // TODO: wrong ?
			{ VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT }, VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.FlushBarriers( cmdBuff );
	}
};

struct vbuffer_pass
{
	static constexpr VkFormat	DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
	static constexpr VkFormat	VBUFF_FORMAT = VK_FORMAT_R32G32_UINT;

	vk_image					colorTarget;
	vk_image					depthTarget;

	VkPipeline					gfxVBuffPipeline;
	VkPipeline					compDbgHashTriToScPipeline;

	desc_hndl32					colSrv;
	desc_hndl32					depthSrv;

	void Init( vk_context& dc, u16 width, u16 height )
	{
		depthTarget = dc.CreateImage( {
			.name		= "Img_DepthTarget",
			.format		= DEPTH_FORMAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.width		= width,
			.height		= height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		depthSrv = dc.AllocDescriptorIdx( { depthTarget.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		colorTarget = dc.CreateImage( {
			.name		= "Img_VBufferTarget",
			.format		= VBUFF_FORMAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.width		= width,
			.height		= height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		colSrv = dc.AllocDescriptorIdx( { colorTarget.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		{
			unique_shader_ptr vtx = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/vertex_VBufferVsMain.spirv" ) );
			unique_shader_ptr frag = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/pixel_VBufferPsMain.spirv" ) );

			// TODO: in order to render properly these must
			// be tied to the world space and the asset tri winding ( which has TO BE in that space )
			vk_gfx_pso_config vbuffState = {
				.polyMode			= VK_POLYGON_MODE_FILL,
				.cullFlags			= HT_CULL_MODE,
				.frontFace			= HT_FRONT_FACE,
				.primTopology		= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				.depthWrite			= true,
				.depthTestEnable	= true,
				.blendCol			= false
			};

			vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

			gfxVBuffPipeline = dc.CreateGfxPipeline( shaderStages, dynamicStates, &VBUFF_FORMAT,
				1, DEPTH_FORMAT, vbuffState, dc.globalPipelineLayout );
		}
		{
			unique_shader_ptr comp = dc.CreateShaderFromSpirv(
				ReadFileBinary( "bin/SpirV/compute_VBufferDbgDrawCsMain.spirv" ) );
			compDbgHashTriToScPipeline = dc.CreateComputePipeline( *comp );
		}
	}

	void DrawIndexedIndirect(
		vk_command_buffer&		cmdBuff,
		vk_rsc_state_tracker&	rscTracker,
		const vk_buffer&      	indexBuff,
		VkIndexType           	indexType,
		const vk_buffer&		drawCmds,
		const vk_buffer&		drawCount,
		const vk_buffer&		visClustersBuff,
		desc_hndl32				drawBuffIdx,
		desc_hndl32				visClustersBuffIdx,
		desc_hndl32				instBuffIdx,
		desc_hndl32				camIdx,
		bool					latePass
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "VBuffer Pass", {} );

		rscTracker.UseImage( depthTarget,
			{ VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, HT_FRAGMENT_TESTS_STAGE },
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscTracker.UseImage( colorTarget,
			{ VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT },
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );

		rscTracker.UseBuffer( drawCmds, { VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT } );
		rscTracker.UseBuffer( drawCount, { VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT } );
		rscTracker.UseBuffer( visClustersBuff, { VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT } );
		rscTracker.FlushBarriers( cmdBuff );

		// NOTE: since we do this incrementally we want the 2nd pass to just load
		const VkAttachmentLoadOp loadOp = latePass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;

		VkClearValue clear = { .color = GetVBufferClearValue() };

		VkRenderingAttachmentInfo attInfos[] = {
			VkMakeAttachmentInfo( colorTarget.view, loadOp, VK_ATTACHMENT_STORE_OP_STORE, clear )
		};
		VkRenderingAttachmentInfo depthWrite = VkMakeAttachmentInfo( depthTarget.view, loadOp,
			VK_ATTACHMENT_STORE_OP_STORE, { .depthStencil = REV_Z_DEPTH_BUFFER_CLEAR_VAL } );

		vk_rendering_info renderingInfo = {
			.viewport = VkCorrectedGetViewport( colorTarget.width, colorTarget.height ),
			.scissor = VkGetScissor( colorTarget.width, colorTarget.height ),
			.colorAttachments = attInfos,
			.pDepthAttachment = &depthWrite
		};

		vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

		cmdBuff.CmdBindPipelineAndBindlessDesc( gfxVBuffPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS );

		vbuffer_params pushBlock = {
			.drawBuffIdx	= drawBuffIdx.slot,
			.visMltBuffIdx	= visClustersBuffIdx.slot,
			.instBuffIdx	= instBuffIdx.slot,
			.camIdx			= camIdx.slot
		};
		cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
		cmdBuff.CmdDrawIndexedIndirectCount<draw_indexed_command>( indexBuff, indexType, drawCmds, drawCount );
	}

	void DebugDrawHashedVBuffer(
		vk_command_buffer&			cmdBuff,
		vk_rsc_state_tracker&		rscTracker,
		const vk_image&				dstImg,
		desc_hndl32					dstImgIdx
	) {
		HT_ASSERT( ( colorTarget.width == dstImg.width ) && ( colorTarget.height == dstImg.height ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "VBuffer Dbg Tri hash Pass", {} );

		rscTracker.UseImage( dstImg, { VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT },
			VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.UseImage( colorTarget,
			{ VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT },
			VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.FlushBarriers( cmdBuff );

		cmdBuff.CmdBindPipelineAndBindlessDesc( compDbgHashTriToScPipeline, VK_PIPELINE_BIND_POINT_COMPUTE );

		vbuffer_dbg_draw_params pushBlock = { .srcIdx = colSrv.slot, .dstIdx = dstImgIdx.slot };
		cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );

		group_size groupSize = { 16, 16, 1 };
		u32x3 numWorkGrs = {
			GroupCount( colorTarget.width, groupSize.x ),
			GroupCount( colorTarget.height, groupSize.y ), 1
		};
		cmdBuff.CmdDispatch( numWorkGrs );
	}
};


#include "ht_gfx_types.h"
#include "hell_pack.h"

using offset_alloc_t = OffsetAllocator::Allocation;

struct offset_allocator_t
{
	static constexpr u64		ALIGNMENT = alignof( OffsetAllocator::Allocator );

	alignas( ALIGNMENT ) u8		mStorage[ sizeof( OffsetAllocator::Allocator ) ] = {};
	OffsetAllocator::Allocator*	mAlloc = {};

	offset_allocator_t() = default;
	offset_allocator_t( u32 size, u32 maxAllocs = 128 * 1024 )
	{
		mAlloc = new ( mStorage ) OffsetAllocator::Allocator( size, maxAllocs );
	}

	~offset_allocator_t() { if ( mAlloc ) { mAlloc->~Allocator(); } }

	offset_allocator_t( offset_allocator_t&& o )
	{
		if ( o.mAlloc )
		{
			mAlloc = new ( mStorage ) OffsetAllocator::Allocator( std::move( *o.mAlloc ) );
			o.mAlloc->~Allocator();
			o.mAlloc = {};
		}
	}
	offset_allocator_t& operator=( offset_allocator_t&& o )
	{
		if ( mAlloc )
		{
			mAlloc->~Allocator();
			mAlloc = {};
		}

		if ( o.mAlloc )
		{
			mAlloc = new ( mStorage ) OffsetAllocator::Allocator( std::move( *o.mAlloc ) );
			o.mAlloc->~Allocator();
			o.mAlloc = {};
		}
		return *this;
	}

	offset_alloc_t		Alloc( u32 size )
	{
		OffsetAllocator::Allocation alloc = mAlloc->allocate( size );
		HT_ASSERT( OffsetAllocator::Allocation::NO_SPACE != alloc.offset );
		return alloc;
	}
	void				Free( offset_alloc_t alloc ) { mAlloc->free( alloc ); }
};

struct ht_mesh_component
{
	gpu_mesh					desc;
	offset_alloc_t 				mltAlloc;
	offset_alloc_t 				vtxAlloc;
	offset_alloc_t 				triAlloc;
};


struct virtual_frame
{
	//vk_gpu_timer gpuTimer;
	VkSemaphore                 canGetImgSema;
	vk_buffer	                viewData;

	vk_buffer					gpuMeshTable;
	//vk_buffer					gpuMaterialSlotBuff;

	vk_buffer                   gpuInstances;

	desc_hndl32                 viewDataIdx;
	desc_hndl32					gpuMeshTableDesc;
	//desc_hndl32				gpuMaterialSlotBuffDesc;

	desc_hndl32                 instDesc;

	u32                         fifIdx; // NOTE: for debug
};

inline static virtual_frame MakeVirtualFrame( vk_context& vkCtx, u64 sizeInBytes, u32 fifIdx )
{
	constexpr VkBufferUsageFlags usg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	fixed_string<64> name = { "Buff_VirtualFrame_ViewBuff{}", fifIdx };

	VkSemaphore canGetImgSema = vkCtx.CreateBinarySemaphore();
	vk_buffer viewData = vkCtx.CreateBuffer( {
		.name			= std::data( name ),
		.usageFlags		= usg,
		.sizeInBytes	= sizeInBytes,
		.usage			= buffer_usage::HOST_VISIBLE
	} );
	desc_hndl32 viewDataIdx = vkCtx.AllocDescriptorIdx( viewData );

	constexpr u64 DEFAULT_MESH_TABLE_SIZE = 1024 * sizeof( gpu_mesh );
	fixed_string<64> meshTableName = { "Buff_VirtualFrame_MeshTable{}", fifIdx };

	vk_buffer gpuMeshTable = vkCtx.CreateBuffer( {
		.name			= std::data( meshTableName ),
		.usageFlags		= usg,
		.sizeInBytes	= DEFAULT_MESH_TABLE_SIZE,
		.usage			= buffer_usage::HOST_VISIBLE
	} );
	desc_hndl32 gpuMeshTableDesc = vkCtx.AllocDescriptorIdx( gpuMeshTable );

	constexpr u64 DEFAULT_INST_COUNT = 10'000 * sizeof( instance_desc );
	fixed_string<64> instName = { "Buff_VirtualFrame_Instances{}", fifIdx };

	vk_buffer gpuInstances = vkCtx.CreateBuffer( {
		.name			= std::data( instName ),
		.usageFlags		= usg,
		.sizeInBytes	= DEFAULT_INST_COUNT,
		.usage			= buffer_usage::HOST_VISIBLE
	} );
	desc_hndl32 instDesc = vkCtx.AllocDescriptorIdx( gpuInstances );

	return {
		.canGetImgSema		= canGetImgSema,
		.viewData			= viewData,
		.gpuMeshTable		= gpuMeshTable,
		.gpuInstances		= gpuInstances,
		.viewDataIdx		= viewDataIdx,
		.gpuMeshTableDesc	= gpuMeshTableDesc,
		.instDesc			= instDesc,
		.fifIdx				= fifIdx,
	};
}


struct renderer_context final : renderer_interface
{
	using mesh_hndl32 = slot_vector<ht_mesh_component>::hndl32;
	using fence_hndl32 = slot_vector<VkFence>::hndl32;

	alignas( 8 ) vk_renderer_config         config = {};

	vk_rsc_state_tracker					rscStateTracker;

	culling_pass							cullingPass;
	imgui_pass								imguiPass;
	depth_pyramid_pass						hizbPass;
	tone_mapping_pass						tonemapPass;
	debug_draw_passes						dbgPass;
	vbuffer_pass							vBuffPass;
	// NOTE: will hold all the renderer components, both available and pending upload
	slot_vector<ht_mesh_component>			rendererComponents;

	slot_vector<VkFence>					jobFences;

	fixed_vector<virtual_frame, MAX_FIF>	vrtFrames;

	std::unique_ptr<vk_context>             pVkCtx;

	vk_buffer                               stagingBuff;

	vk_buffer                               megaGpuVtxBuff;
	vk_buffer                               megaGpuTriBuff;
	vk_buffer                               megaGpuMeshletBuff;

	vk_buffer								globalData;

	offset_allocator_t						meshletAllocator;
	offset_allocator_t						vtxAllocator;
	offset_allocator_t						triAllocator;

	u64										vFrameIdx = 0;

	VkSampler								pbrSampler;
	desc_hndl32								pbrSamplerIdx;

	desc_hndl32								globalDataIdx;

	const u32								framesInFlight = 2;


	virtual void InitBackend( u64 hInst, u64 hWnd ) override;

	virtual HRNDMESH32 AllocMeshComponent( const hellpack_mesh_asset& mesh ) override;

	inline virtual HJOBFENCE32 AllocJobFence() override
	{
		return std::bit_cast<HJOBFENCE32>( jobFences.PushEntry( pVkCtx->AllocFence() ) );
	}
	inline virtual bool PollJobFenceAndRemoveOnCompletion( HJOBFENCE32 hJobFence, u64 timeoutNanosecs ) override
	{
		VkFence fence = jobFences[ ( fence_hndl32 ) hJobFence ];
		return pVkCtx->FenceWaitAndResetOnDone( fence, timeoutNanosecs );
	}
	virtual void UploadMeshes(
		HJOBFENCE32							hRndUpload,
		std::span<const mesh_upload_req>	meshAssets,
		virtual_arena&						arena
	) override;

	u32 /* numValidInstances */ UpdateSceneData( const virtual_frame& thisVFrame, const frame_data& frameData )
	{
		HT_ASSERT( BYTE_COUNT( frameData.views ) <= thisVFrame.viewData.sizeInBytes );
		std::memcpy( thisVFrame.viewData.hostVisible, std::data( frameData.views ), BYTE_COUNT( frameData.views ) );

		ht_stretchybuff<gpu_mesh> gpuMeshTable = HtNewStretchyBuffFromMem<gpu_mesh>(
			thisVFrame.gpuMeshTable.hostVisible, thisVFrame.gpuMeshTable.sizeInBytes );
		// NOTE: for now we alloc for worst scenario and copy it with invalid slots too, those won't be accessed anyways
		HT_ASSERT( std::size( rendererComponents ) <= gpuMeshTable.capacity() );

		for( const ht_mesh_component& component : rendererComponents.items )
		{
			gpu_mesh gpuMesh = {};
			if( !slot_vector<ht_mesh_component>::IsDeadSlot( component ) )
			{
				gpuMesh = component.desc;
			}
			gpuMeshTable.push_back( gpuMesh );
		}

		ht_stretchybuff<gpu_instance> gpuInstList = HtNewStretchyBuffFromMem<gpu_instance>(
			thisVFrame.gpuInstances.hostVisible, thisVFrame.gpuInstances.sizeInBytes );
		HT_ASSERT( std::size( frameData.instances ) <= gpuInstList.capacity() );

		for( const instance_desc& sceneNode : frameData.instances )
		{
			mesh_hndl32 hMesh = std::bit_cast<mesh_hndl32>( sceneNode.meshIdx );
			if( IsStructZero( rendererComponents[ hMesh ] ) )
			{
				continue;
			}
			gpuInstList.push_back( {
				.toWorld = TrsToFloat4x3RowMaj( sceneNode.transform ),
				.meshIdx = hMesh.slotIdx
				//.mtrlIdx =
			} );
		}

		return ( u32 ) std::size( gpuInstList );
	}

	virtual void HostFrames( const frame_data& frameData, gpu_data& gpuData ) override;
};

std::unique_ptr<renderer_interface> MakeRenderer()
{
	return std::make_unique<renderer_context>();
}

void renderer_context::InitBackend( u64 hInst, u64 hWnd )
{
	config = { .renderWidth = SCREEN_WIDTH, .renderHeight = SCREEN_HEIGHT };

	pVkCtx = std::make_unique<vk_context>( VkMakeContext( hInst, hWnd, config ) );

	globalData = pVkCtx->CreateBuffer( {
		.usageFlags		= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		.sizeInBytes	= sizeof( global_data ),
		.usage			= buffer_usage::HOST_VISIBLE
	} );
	globalDataIdx = pVkCtx->AllocDescriptorIdx( globalData );
	// NOTE: this is a workaround, must be first so we match idx 0
	HT_ASSERT( GLOB_DATA_BINDING_SLOT == globalDataIdx.slot );

	cullingPass.Init( *pVkCtx );
	tonemapPass.Init( *pVkCtx );
	hizbPass.Init( *pVkCtx );
	dbgPass.Init( *pVkCtx, config );

	imguiPass = MakeImguiPass( *pVkCtx, pVkCtx->scConfig.format );

	stagingBuff = pVkCtx->CreateBuffer( {
		.name			= "StagingBuff",
		.usageFlags		= VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sizeInBytes	= 256 * MB,
		.usage			= buffer_usage::STAGING
	} );

	constexpr VkBufferUsageFlags megaBuffUsg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	megaGpuMeshletBuff = pVkCtx->CreateBuffer( {
		.name			= "MegaGpuMeshletBuff",
		.usageFlags		= megaBuffUsg,
		.sizeInBytes	= sizeof( gpu_meshlet ) * MAX_MESHLETS_IN_SCENE,
		.usage			= buffer_usage::GPU_ONLY
	} );
	megaGpuVtxBuff = pVkCtx->CreateBuffer( {
		.name			= "MegaGpuVtxBuff",
		.usageFlags		= megaBuffUsg,
		.sizeInBytes	= sizeof( packed_vtx ) * MAX_VERTICES_IN_SCENE,
		.usage			= buffer_usage::GPU_ONLY
	} );

	// NOTE: we align to u32 bc we read it in 4 bytes chunks in the shader and this prevents any out of
	// bounds accesses, essentially we'll read garbage data safely
	u64 triMegaBuffSzInBytes = FwdAlign( sizeof( index_t ) * MAX_TRIANGLES_IN_SCENE, sizeof( u32 ) );
	megaGpuTriBuff = pVkCtx->CreateBuffer( {
		.name			= "MegaGpuTriBuff",
		.usageFlags		= megaBuffUsg | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		.sizeInBytes	= triMegaBuffSzInBytes,
		.usage			= buffer_usage::GPU_ONLY
	} );
	HT_ASSERT( FwdAlign( megaGpuTriBuff.sizeInBytes, sizeof( u64 ) ) == megaGpuTriBuff.sizeInBytes );

	meshletAllocator= { ( u32 ) megaGpuMeshletBuff.sizeInBytes };
	vtxAllocator	= { ( u32 ) megaGpuVtxBuff.sizeInBytes };
	triAllocator	= { ( u32 ) megaGpuTriBuff.sizeInBytes };

	// TODO: move
	VkSamplerCreateInfo samplerCreateInfo = {
		.sType						= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter					= VK_FILTER_LINEAR,
		.minFilter					= VK_FILTER_LINEAR,
		.mipmapMode					= VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxAnisotropy				= 1.0f,
		.minLod						= 0,
		.maxLod						= VK_LOD_CLAMP_NONE,
		.borderColor				= VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates	= VK_FALSE,
	};

	pbrSampler		= pVkCtx->CreateSampler( samplerCreateInfo );
	pbrSamplerIdx	= pVkCtx->AllocDescriptorIdx( { pbrSampler } );
}

HRNDMESH32 renderer_context::AllocMeshComponent( const hellpack_mesh_asset& mesh )
{
	byte_view mltAsBytes = AsBytes( mesh.meshlets );
	byte_view vtxAsBytes = AsBytes( mesh.vertices );
	byte_view triAsBytes = AsBytes( mesh.triangles );

	HT_ASSERT( std::size( mltAsBytes ) );
	HT_ASSERT( std::size( vtxAsBytes ) );
	HT_ASSERT( std::size( triAsBytes ) );

	offset_alloc_t mltAlloc	= meshletAllocator.Alloc( ( u32 ) std::size( mltAsBytes ) );
	offset_alloc_t vtxAlloc	= vtxAllocator.Alloc( ( u32 ) std::size( vtxAsBytes ) );
	offset_alloc_t triAlloc	= triAllocator.Alloc( ( u32 ) std::size( triAsBytes ) );

	// NOTE: this MUST be in elements bc we use it on the gpu as such
	gpu_mesh gpuMesh = {
		.minAabb		= mesh.aabbMin,
		.maxAabb		= mesh.aabbMax,
		.meshletOffset	= mltAlloc.offset / mesh.meshlets.STRIDE,
		.vtxOffset		= vtxAlloc.offset / mesh.vertices.STRIDE,
		.triOffset		= triAlloc.offset / mesh.triangles.STRIDE,
		.meshletCount	= ( u32 ) std::size( mesh.meshlets ),
		.vtxCount		= ( u32 ) std::size( mesh.vertices ),
		.triCount		= ( u32 ) std::size( mesh.triangles ) // NOTE: in this particular case it will double as byte count too
	};

	ht_mesh_component htMesh = { .desc = gpuMesh, .mltAlloc = mltAlloc, .vtxAlloc = vtxAlloc, .triAlloc = triAlloc };

	return std::bit_cast<u32>( rendererComponents.PushEntry( htMesh ) );
}

void renderer_context::UploadMeshes(
	HJOBFENCE32							hRndUpload,
	std::span<const mesh_upload_req>	meshUploadReqs,
	virtual_arena&						arena
) {
	stack_adaptor<virtual_arena> vaStack = { arena };

	ht_stretchybuff<u8> stagingScratch = HtNewStretchyBuffFromMem<u8>( stagingBuff.hostVisible, stagingBuff.sizeInBytes  );

	u64 barrierCount = std::size( meshUploadReqs ) * 3;
	u64 copyCmdCount = std::size( meshUploadReqs );

	std::pmr::vector<VkBufferMemoryBarrier2> buffInitCpyBarriers{ &vaStack };
	buffInitCpyBarriers.reserve( barrierCount );

	std::pmr::vector<VkBufferCopy2> mltRegionCopies{ &vaStack };
	mltRegionCopies.reserve( copyCmdCount );
	std::pmr::vector<VkBufferCopy2> vtxRegionCopies{ &vaStack };
	vtxRegionCopies.reserve( copyCmdCount );
	std::pmr::vector<VkBufferCopy2> triRegionCopies{ &vaStack };
	triRegionCopies.reserve( copyCmdCount );

	std::pmr::vector<VkBufferMemoryBarrier2> buffEndCpyBarriers{ &vaStack };
	buffEndCpyBarriers.reserve( barrierCount );

	auto CopyScaffoldingLambda = [ & ] (
		const vk_buffer&					dstBuff,
		std::pmr::vector<VkBufferCopy2>&	regionCopies,
		byte_view							bytesSrc,
		u32									dstOffsetInBytes
	){
		u64 srcOffsetInBytes = std::size( stagingScratch );
		stagingScratch.append_range( bytesSrc );

		u64 payloadSizeInBytes = std::size( bytesSrc );

		buffInitCpyBarriers.push_back( VkMakeBufferBarrier( dstBuff.hndl, 0, 0,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			dstOffsetInBytes, payloadSizeInBytes ) );

		regionCopies.push_back( MakeVkBufferCopy2( srcOffsetInBytes, dstOffsetInBytes, payloadSizeInBytes ) );

		buffEndCpyBarriers.push_back( VkMakeBufferBarrier( dstBuff.hndl, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT, 0, 0,
			dstOffsetInBytes, payloadSizeInBytes,
			pVkCtx->copyQueue.familyIdx, pVkCtx->gfxQueue.familyIdx ) );
	};

	for( const mesh_upload_req& meshUpload : meshUploadReqs )
	{
		const ht_mesh_component& htMesh = rendererComponents[ ( mesh_hndl32 ) meshUpload.hSlot ];

		CopyScaffoldingLambda( megaGpuMeshletBuff, mltRegionCopies, meshUpload.mltAsBytes, htMesh.mltAlloc.offset );
		CopyScaffoldingLambda( megaGpuVtxBuff, vtxRegionCopies, meshUpload.vtxAsBytes, htMesh.vtxAlloc.offset );
		CopyScaffoldingLambda( megaGpuTriBuff, triRegionCopies, meshUpload.triAsBytes, htMesh.triAlloc.offset );
	}

	vk_cmd_pool_buff copyCB = pVkCtx->AllocateCmdPoolAndBuff( vk_queue_t::COPY );
	vk_command_buffer copyCmdBuff = { copyCB.buff, VK_NULL_HANDLE, VK_NULL_HANDLE };

	copyCmdBuff.CmdPipelineBufferBarriers( buffInitCpyBarriers );

	copyCmdBuff.CmdCopyBuffer( stagingBuff, megaGpuMeshletBuff, mltRegionCopies );
	copyCmdBuff.CmdCopyBuffer( stagingBuff, megaGpuVtxBuff, vtxRegionCopies );
	copyCmdBuff.CmdCopyBuffer( stagingBuff, megaGpuTriBuff, triRegionCopies );

	copyCmdBuff.CmdPipelineBufferBarriers( buffEndCpyBarriers );

	copyCmdBuff.CmdEndCmdBuffer();

	pVkCtx->QueueSubmit( pVkCtx->copyQueue, copyCB );

	vk_cmd_pool_buff gfxCB = pVkCtx->AllocateCmdPoolAndBuff( vk_queue_t::GFX );
	vk_command_buffer gfxCmdBuff = { gfxCB.buff, VK_NULL_HANDLE, VK_NULL_HANDLE };

	std::pmr::vector<VkBufferMemoryBarrier2> buffTransferOwnershipBarriers{ &vaStack };
	buffTransferOwnershipBarriers.reserve( barrierCount );

	for( const VkBufferMemoryBarrier2& barr : buffEndCpyBarriers )
	{
		buffTransferOwnershipBarriers.push_back( VkMakeBufferBarrier( barr.buffer, 0, 0,
			0, 0, barr.offset, barr.size,
			pVkCtx->copyQueue.familyIdx, pVkCtx->gfxQueue.familyIdx ) );
	}

	gfxCmdBuff.CmdPipelineBufferBarriers( buffTransferOwnershipBarriers );

	gfxCmdBuff.CmdEndCmdBuffer();

	VkSemaphoreSubmitInfo waitCpyDone[] = { {
		.sType		= VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore	= pVkCtx->copyQueue.timelineSema,
		.value		= pVkCtx->copyQueue.submitionCount,
		.stageMask	= VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
	} };

	pVkCtx->QueueSubmit( pVkCtx->gfxQueue, gfxCB, waitCpyDone, {}, jobFences[ ( fence_hndl32 ) hRndUpload ] );
}

void renderer_context::HostFrames( const frame_data& frameData, gpu_data& gpuData )
{
	const u64 currentFrameIdx			= vFrameIdx++;
	const u64 currentFrameInFlightIdx	= currentFrameIdx % framesInFlight;

	[[unlikely]]
	if( currentFrameIdx < framesInFlight )
	{
		virtual_frame vFrame = MakeVirtualFrame( *pVkCtx,  std::size( frameData.views ) * sizeof( view_data ),
			( u32 ) currentFrameInFlightIdx );
		vrtFrames.push_back( vFrame );
	}

	VkResult timelineWaitResult = pVkCtx->TimelineTryWaitFor( pVkCtx->gpuFrameTimeline,
		framesInFlight, UINT64_MAX );
	HT_ASSERT( timelineWaitResult < VK_TIMEOUT );

	pVkCtx->FlushDeletionQueues( currentFrameIdx );

	const virtual_frame& thisVFrame = vrtFrames[ currentFrameInFlightIdx ];

	u32 instCount = UpdateSceneData( thisVFrame, frameData );

	HT_ASSERT( instCount <= MAX_INSTANCES_IN_SCENE );
	// TODO: same for meshlets and the rest ????

	vk_cmd_pool_buff currentCB = pVkCtx->AllocateCmdPoolAndBuff( vk_queue_t::GFX );
	vk_command_buffer thisFrameCmdBuffer = { currentCB.buff, pVkCtx->globalPipelineLayout, pVkCtx->descSet };

	static bool initResources = false;
	if( !initResources )
	{
		pVkCtx->CreateSwapchain();

		vBuffPass.Init( *pVkCtx, config.renderWidth, config.renderHeight );
		rscStateTracker.UseImage( vBuffPass.depthTarget, {}, VK_IMAGE_LAYOUT_UNDEFINED );
		rscStateTracker.UseImage( vBuffPass.colorTarget, {}, VK_IMAGE_LAYOUT_UNDEFINED );

		global_data& refGD = *( global_data* ) globalData.hostVisible;
		refGD = {
			.mltAddr = megaGpuMeshletBuff.devicePointer,
			.vtxAddr = megaGpuVtxBuff.devicePointer,
			.triAddr = megaGpuTriBuff.devicePointer
		};

		imguiPass.CreateUploadFontAtlasSync( *pVkCtx, thisFrameCmdBuffer, currentFrameIdx );

		// TODO: add UploadDataSync function in the renderer
		cullingPass.InitSceneDependentData( *pVkCtx, MAX_INSTANCES_IN_SCENE );

		dbgPass.InitAndUploadDebugGeometry( *pVkCtx );

		rscStateTracker.UseBuffer( tonemapPass.averageLuminanceBuffer, TRANSFER_WRITE );

		thisFrameCmdBuffer.CmdFillVkBuffer( tonemapPass.averageLuminanceBuffer, 0u );

		rscStateTracker.UseBuffer( tonemapPass.averageLuminanceBuffer, COMPUTE_READWRITE );

		rscStateTracker.UseImage( hizbPass.hiZTarget, {}, VK_IMAGE_LAYOUT_UNDEFINED );

		rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

		initResources = true;
	}

	pVkCtx->FlushPendingDescriptorUpdates();

	u32 scImgIdx = pVkCtx->AcquireNextSwapchainImageBlocking( thisVFrame.canGetImgSema );
	const vk_swapchain_image& scImg = pVkCtx->scImgs[ scImgIdx ];

	// TODO: don't hardcode here
	const u32 camIdx = !frameData.dbgDrawFlags.freezeMainView ? 0 : 1;
	{
		dbgPass.ResetDrawCounters( thisFrameCmdBuffer, rscStateTracker );

		const culling_pass_args cullPassArgs = {
			.dbgGpuInstBuff			= dbgPass.gpuInstBuff,
			.dbgGpuInstCountBuff	= dbgPass.gpuInstCountBuff,
			.hiZTarget				= hizbPass.hiZTarget,
			.instCount				= instCount,
			.instBuffIdx			= thisVFrame.instDesc,
			.meshTableIdx			= thisVFrame.gpuMeshTableDesc,
			.viewBuffIdx			= thisVFrame.viewDataIdx,
			.camIdx					= camIdx,
			.hizDesc				= hizbPass.hizSrv,
			.samplerDesc			= hizbPass.quadMinSamplerIdx,
			.dbgGpuInstBuffIdx		= dbgPass.gpuInstBuffIdx,
			.dbgGpuInstCountBuffIdx = dbgPass.gpuInstCountBuffIdx
		};
		cullingPass.Execute( thisFrameCmdBuffer, rscStateTracker, cullPassArgs, false );

		vBuffPass.DrawIndexedIndirect( thisFrameCmdBuffer, rscStateTracker, megaGpuTriBuff,
			VK_INDEX_TYPE_UINT8, cullingPass.drawCmds, cullingPass.drawCount, cullingPass.visibleClusters,
			cullingPass.drawCmdsIdx, cullingPass.visibleClustersIdx, thisVFrame.instDesc,
			thisVFrame.viewDataIdx, false );

		hizbPass.Execute( thisFrameCmdBuffer, rscStateTracker, vBuffPass.depthTarget, vBuffPass.depthSrv );

		cullingPass.Execute( thisFrameCmdBuffer, rscStateTracker, cullPassArgs, true );

		vBuffPass.DrawIndexedIndirect( thisFrameCmdBuffer, rscStateTracker, megaGpuTriBuff,
			VK_INDEX_TYPE_UINT8, cullingPass.drawCmds, cullingPass.drawCount, cullingPass.visibleClusters,
			cullingPass.drawCmdsIdx, cullingPass.visibleClustersIdx, thisVFrame.instDesc,
			thisVFrame.viewDataIdx, true );

		hizbPass.Execute( thisFrameCmdBuffer, rscStateTracker, vBuffPass.depthTarget, vBuffPass.depthSrv );

		// NOTE: we need an exec dependency between AcquireNextSwapchainImageBlocking and the compute write
		constexpr VkPipelineStageFlags2 execDep =
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		rscStateTracker.UseImage( scImg.img, { 0, execDep }, VK_IMAGE_LAYOUT_UNDEFINED );

		rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

		[[unlikely]]
		if( !frameData.dbgDrawFlags.vBuffPixelHash )
		{
			lambertian_clay_params pushBlock = {
				.texResolution	= { ( float ) scImg.img.width, ( float ) scImg.img.height },
				.vbuffIdx		= vBuffPass.colSrv.slot,
				.dstIdx			= scImg.writeDescIdx.slot,
				.instBuffIdx	= thisVFrame.instDesc.slot,
				.meshDescIdx 	= thisVFrame.gpuMeshTableDesc.slot,
				.camIdx			= thisVFrame.viewDataIdx.slot
			};
			dbgPass.DrawAsLamberitanClay(
				thisFrameCmdBuffer, rscStateTracker, vBuffPass.colorTarget, scImg.img, pushBlock );

			//tonemapPass.AverageLuminancePass( thisFrameCmdBuffer, rscStateTracker, vBuffPass.colorTarget, vBuffPass.colSrv,
			//	frameData.elapsedSeconds );
			//
			//u32x2 colorTargetSize = { vBuffPass.colorTarget.width, vBuffPass.colorTarget.height };
			//
			//tonemapPass.TonemappingGammaPass( thisFrameCmdBuffer, rscStateTracker, scImg.img, vBuffPass.colSrv,
			//	scImg.writeDescIdx, colorTargetSize );
		}
		else
		{
			vBuffPass.DebugDrawHashedVBuffer( thisFrameCmdBuffer, rscStateTracker, scImg.img, scImg.writeDescIdx );
		}

		[[unlikely]]
		if( frameData.dbgDrawFlags.dbgDraw )
		{
			dbgPass.DrawWireframesGPU( thisFrameCmdBuffer, rscStateTracker, scImg.img, thisVFrame.viewDataIdx );
		}

		[[unlikely]]
		if( frameData.dbgDrawFlags.freezeMainView )
		{
			dbgPass.cpuInstView.resize( 0 );
			dbgPass.cpuInstView.push_back( {
				.toWorld	= frameData.frustTransf,
				.color		= DXPackedXMColorToFloat4( HT_CYAN ),
				.minAabb	= BOX_MIN,
				.maxAabb	= BOX_MAX
			} );
			dbgPass.DrawWireframeCPU( thisFrameCmdBuffer, rscStateTracker, scImg.img, thisVFrame.viewDataIdx );
		}

		rscStateTracker.UseImage( scImg.img,
			{ HT_COLOR_ATTACHMENT_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT },
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

		imguiPass.DrawUiPass( *pVkCtx, thisFrameCmdBuffer.hndl, scImg.img, currentFrameIdx, currentFrameInFlightIdx );

		rscStateTracker.UseImage( scImg.img, { 0, 0 }, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
		rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

		// NOTE: remove sc image to avoid handling this logic inside the tracker
		rscStateTracker.StopTrackingResource( ( u64 ) scImg.img.hndl );
	}

	thisFrameCmdBuffer.CmdEndCmdBuffer();

	VkSemaphoreSubmitInfo waitScImgAcquire[] = { {
		.sType		= VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore	= thisVFrame.canGetImgSema,
		.stageMask	= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	} };
	VkSemaphoreSubmitInfo signalRenderFinished[] = {
		{
			.sType		= VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore	= scImg.canPresentSema,
			.stageMask	= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		},
		pVkCtx->gpuFrameTimeline.GetSignalNextPoint( VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT )
	};

	pVkCtx->QueueSubmit( pVkCtx->gfxQueue, currentCB, waitScImgAcquire, signalRenderFinished );
	pVkCtx->QueuePresent( pVkCtx->gfxQueue, scImgIdx );
}
