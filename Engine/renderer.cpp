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


//====================CONSTS====================//
constexpr u64 				MAX_FIF					= vk_renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED;
constexpr u64 				MAX_TRIANGLES_IN_SCENE	= 10'000'000;
constexpr u64 				MAX_VERTICES_IN_SCENE	= 5'000'000;
constexpr u64 				MAX_MESHLETS_IN_SCENE	= 100'000;
constexpr u64 				MAX_INSTANCES_IN_SCENE	= 10'000;
constexpr VkCullModeFlags	HT_CULL_MODE			= IS_WORLD_RH ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
constexpr VkFrontFace		HT_FRONT_FACE			= IS_WORLD_RH ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
//==============================================//
// TODO: cvars
//====================CVARS=====================//

//==============================================//

//==============CONSTEXPR_SWITCH================//

//==============================================//

#include "ht_renderer_types.h"

// NOTE: clear depth to 0 bc we	use RevZ
constexpr VkClearValue DEPTH_CLEAR_VAL	= {};
constexpr VkClearValue RT_CLEAR_VAL		= {};

enum render_target_op : u32
{
	LOAD		= VK_ATTACHMENT_LOAD_OP_LOAD,
	LOAD_CLEAR	= VK_ATTACHMENT_LOAD_OP_CLEAR,
	STORE		= VK_ATTACHMENT_STORE_OP_STORE
};


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

		// Textures

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
		.depthCompareOp			= VK_COMPARE_OP_NEVER,
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


struct debug_draw_passes
{
	// TODO: where to place these ?
	static constexpr box_vertices			unitCube	= GenerateDbgBoxFromBounds( BOX_MIN, BOX_MAX );
	static constexpr box_wireframe_indices	lineVtxBuff = GenerateBoxWireframeIndices();
	//constexpr box_triangle_indices trisVtxBuff = BoxVerticesAsTriangles( unitCube );

	ht_stretchybuff<dbg_aabb_instance>		cpuInstView = {};

	vk_buffer			vtxBuff 			= {};
	vk_buffer			idxBuff 			= {};
	vk_buffer			gpuInstBuff			= {};
	vk_buffer			gpuInstCountBuff	= {};
	vk_buffer			cpuInstBuff			= {};

	vk_buffer			drawCmdsBuff		= {};
	vk_buffer			drawCountBuff 		= {};

	VkPipeline			drawAsLines 		= {};
	VkPipeline			drawAsTriangles		= {};
	// NOTE: it's easier to separate the recording of instances and the submission of draws
	vk_compute_pipeline	compRecordDbgDraw	= {};
	vk_compute_pipeline	compLambertianClay	= {};

	desc_hndl32			gpuInstBuffIdx		= {};
	desc_hndl32			gpuInstCountBuffIdx = {};

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
				.depthCompareOp		= VK_COMPARE_OP_NEVER,
				.depthWrite			= false,
				.depthTestEnable	= false,
				.blendCol			= false,
			};

			drawAsLines = dc.CreateGfxPipeline( gfxStages, dynamicStates, &rndCfg.desiredColorFormat,
				1, VK_FORMAT_UNDEFINED, lineDrawPipelineState );
		}

		//vk_gfx_pso_config triDrawPipelineState = {
		//	.polyMode			= VK_POLYGON_MODE_FILL,
		//	.cullFlags			= VK_CULL_MODE_NONE,
		//	.frontFace			= VK_FRONT_FACE_COUNTER_CLOCKWISE,
		//	.primTopology		= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		//	.depthWrite			= VK_TRUE,
		//	.depthTestEnable	= VK_TRUE,
		//	.blendCol			= VK_TRUE
		//};
		//
		//drawAsTriangles = dc.CreateGfxPipeline( gfxStages, dynamicStates, &rndCfg.desiredColorFormat,
		//	1, rndCfg.desiredDepthFormat, triDrawPipelineState );

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
			.name			= "Buff_DbgVtx",
			.usageFlags		=  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sizeInBytes	= 500 * sizeof( dbg_vertex ),
			.usage			= buffer_usage::HOST_VISIBLE
		} );

		idxBuff = dc.CreateBuffer( {
			.name			= "Buff_DbgIdx",
			.usageFlags		= VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sizeInBytes	= 2000 * sizeof( dbg_index_t ),
			.usage			= buffer_usage::HOST_VISIBLE
		} );

		std::memcpy( vtxBuff.hostVisible, std::data( unitCube ), BYTE_COUNT( unitCube ) );
		std::memcpy( idxBuff.hostVisible, std::data( lineVtxBuff ), BYTE_COUNT( lineVtxBuff ) );
	}

	inline void ResetDrawCounters( vk_command_buffer& cmdBuff, vk_rsc_state_tracker& rscTracker )
	{
		rscTracker.UseBuffer( drawCountBuff, HT_TRANSFER_WRITE );
		rscTracker.UseBuffer( gpuInstCountBuff, HT_TRANSFER_WRITE );

		rscTracker.FlushBarriers( cmdBuff );

		cmdBuff.CmdFillBuffer( drawCountBuff, 0u );
		cmdBuff.CmdFillBuffer( gpuInstCountBuff, 0u );
	}

	void DrawWireframesGPU(
		vk_command_buffer&      		cmdBuff,
		vk_rsc_state_tracker&			rscTracker,
		const vk_image&  				colorTarget,
		desc_hndl32						camIdx
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Dbg_DrawWireframesGPU", {} );

		rscTracker.UseBuffer( drawCountBuff, HT_COMPUTE_WRITE );
		rscTracker.UseBuffer( drawCmdsBuff, HT_COMPUTE_WRITE );
		rscTracker.UseBuffer( gpuInstCountBuff, HT_COMPUTE_READ );
		rscTracker.FlushBarriers( cmdBuff );

		{
			record_dbg_draw_params pushBlock = {
				.gpuInstCountAddr	= gpuInstCountBuff.devicePointer,
				.dbgDrawCmdsAddr	= drawCmdsBuff.devicePointer,
				.dbgDrawCountAddr	= drawCountBuff.devicePointer,
				.indexCount			= ( u32 ) std::size( lineVtxBuff ),
				.firstIndex			= 0,
				.vertexOffset		= 0
			};
			cmdBuff.DispatchCompute( compRecordDbgDraw, pushBlock, { 1, 1, 1 } );
		}

		rscTracker.UseBuffer( drawCmdsBuff, HT_DRAW_INDIRECT_READ );
		rscTracker.UseBuffer( drawCountBuff, HT_DRAW_INDIRECT_READ );
		rscTracker.UseBuffer( gpuInstBuff, { VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT } );
		rscTracker.UseImage( colorTarget, HT_COLOR_TARGET_OUT_READWRITE, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );

		rscTracker.FlushBarriers( cmdBuff );

		{
			VkRenderingAttachmentInfo attInfos[] = {
				VkMakeAttachmentInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} )
			};

			vk_rendering_info renderingInfo = {
				.colorAttachments	= attInfos,
				.pDepthAttachment	= nullptr,
				.viewport			= VkCorrectedGetViewport( colorTarget.width, colorTarget.height ),
				.scissor			= VkGetScissor( colorTarget.width, colorTarget.height )
			};

			vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

			cmdBuff.CmdBindPipelineAndBindlessDesc( drawAsLines, VK_PIPELINE_BIND_POINT_GRAPHICS );

			dbg_box_params pushBlock = {
				.instBuffAddr	= gpuInstBuff.devicePointer, // NOTE: it's GPU !!!!
				.vtxBuffAddr	= vtxBuff.devicePointer,
				.camIdx			= camIdx.slot
			};
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDrawIndexedIndirectCount<draw_instanced_indexed_indirect>( idxBuff, VK_INDEX_TYPE_UINT16,
				drawCmdsBuff, drawCountBuff );
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

		rscTracker.UseImage( colorTarget, HT_COLOR_TARGET_OUT_READWRITE, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );

		rscTracker.FlushBarriers( cmdBuff );

		VkRenderingAttachmentInfo attInfos[] = { VkMakeAttachmentInfo( colorTarget.view,
			VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} )
		};

		vk_rendering_info renderingInfo = {
			.colorAttachments	= attInfos,
			.pDepthAttachment	= nullptr,
			.viewport			= VkCorrectedGetViewport( colorTarget.width, colorTarget.height ),
			.scissor			= VkGetScissor( colorTarget.width, colorTarget.height )
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
		const vk_image&					vBuff,
		const vk_image&					dstImg,
		desc_hndl32						vBuffDesc,
		desc_hndl32						dstWriteDesc,
		desc_hndl32						instDesc,
		desc_hndl32						meshTableDesc,
		desc_hndl32						viewDataIdx
	) {
		HT_ASSERT( ( vBuff.width == dstImg.width ) && ( vBuff.height == dstImg.height ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Dbg_LambertianClayPass", {} );

		rscTracker.UseImage( dstImg, HT_COMPUTE_WRITE, VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.UseImage( vBuff,HT_COMPUTE_READ, VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.FlushBarriers( cmdBuff );

		lambertian_clay_params pushBlock = {
			.texResolution	= { ( float ) dstImg.width, ( float ) dstImg.height }, // TODO: WHO's texRes ? be explicit
			.vbuffIdx		= vBuffDesc.slot,
			.dstIdx			= dstWriteDesc.slot,
			.instBuffIdx	= instDesc.slot,
			.meshDescIdx 	= meshTableDesc.slot,
			.camIdx			= viewDataIdx.slot
		};
		cmdBuff.DispatchCompute( compLambertianClay, pushBlock, { dstImg.width, dstImg.height, 1 } );
	}
};


using index_t = u8;

struct culling_pass_args
{
	const vk_buffer&	dbgGpuInstBuff;
	const vk_buffer&	dbgGpuInstCountBuff;
	const vk_image&		hiZTarget;
	u32					instCount;
	desc_hndl32			instBuffIdx;
	desc_hndl32			meshTableIdx;
	desc_hndl32			viewBuffIdx;
	u32					camIdx;
	desc_hndl32			hizDesc;
	desc_hndl32			samplerDesc;
	desc_hndl32			dbgGpuInstBuffIdx;
	desc_hndl32			dbgGpuInstCountBuffIdx;
};

struct culling_pass
{
	vk_buffer			instOccludedCache;

	vk_buffer			visibleInstances;
	vk_buffer			visibleInstCounter;
	vk_buffer			visibleMeshlets;
	vk_buffer			visibleMeshletsCount;

	vk_buffer			occludedMeshlets;
	vk_buffer			occludedMeshletsCount;

	vk_buffer			dispatchIndirect;

	vk_buffer			drawCmds;
	vk_buffer			drawCount;

	vk_compute_pipeline	instCullPass;
	vk_compute_pipeline	instExpansionPass;
	vk_compute_pipeline	indirectDispatchPass;
	vk_compute_pipeline	clusterCullPass;

	desc_hndl32			instOccludedCacheIdx;

	desc_hndl32			visibleInstIdx;
	desc_hndl32			visibleInstCounterIdx;
	desc_hndl32			visibleMeshletsIdx;
	desc_hndl32			visibleMeshletsCountIdx;

	desc_hndl32			occludedMeshletsIdx;
	desc_hndl32			occludedMeshletsCountIdx;

	desc_hndl32			dispatchIndirectIdx;

	desc_hndl32			drawCmdsIdx;
	desc_hndl32			drawCountIdx;


	void Init( vk_context& dc )
	{
		unique_shader_ptr instCull = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/compute_DrawCullCsMain.spirv" ) );
		instCullPass = dc.CreateComputePipeline( *instCull );

		unique_shader_ptr instExp = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/compute_ExpandDrawsCsMain.spirv" ) );
		instExpansionPass = dc.CreateComputePipeline( *instExp );

		unique_shader_ptr dispatcher = dc.CreateShaderFromSpirv(
			ReadFileBinary( "bin/SpirV/compute_IndirectDispatcherCsMain.spirv" ) );
		indirectDispatchPass = dc.CreateComputePipeline( *dispatcher );

		unique_shader_ptr clusterCs = dc.CreateShaderFromSpirv(
			ReadFileBinary( "bin/SpirV/compute_MeshletCullCsMain.spirv" ) );
		clusterCullPass = dc.CreateComputePipeline( *clusterCs );

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
		visibleMeshlets = dc.CreateBuffer( {
			.name			= "Buff_VisibleMeshlets",
			.usageFlags		= usg,
			.sizeInBytes	= MAX_MESHLETS_IN_SCENE * sizeof( visible_meshlet ),
			.usage			= buffer_usage::GPU_ONLY } );
		visibleMeshletsIdx = dc.AllocDescriptorIdx( visibleMeshlets );

		visibleMeshletsCount = dc.CreateBuffer( {
			.name			= "Buff_VisibleMeshletsCount",
			.usageFlags		= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
			| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sizeInBytes	= 1 * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY } );
		visibleMeshletsCountIdx = dc.AllocDescriptorIdx( visibleMeshletsCount );

		occludedMeshlets = dc.CreateBuffer( {
			.name			= "Buff_OccludedMeshlets",
			.usageFlags		= usg,
			.sizeInBytes	= MAX_MESHLETS_IN_SCENE * sizeof( visible_meshlet ),
			.usage			= buffer_usage::GPU_ONLY } );
		occludedMeshletsIdx = dc.AllocDescriptorIdx( occludedMeshlets );

		occludedMeshletsCount = dc.CreateBuffer( {
			.name			= "Buff_OccludedMeshletsCount",
			.usageFlags		= usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes	= 1 * sizeof( u32 ),
			.usage			= buffer_usage::GPU_ONLY } );
		occludedMeshletsCountIdx = dc.AllocDescriptorIdx( occludedMeshletsCount );

		drawCmds = dc.CreateBuffer( {
			.name			= "Buff_DrawCmds",
			.usageFlags		= usg | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT ,
			.sizeInBytes	= MAX_MESHLETS_IN_SCENE * sizeof( draw_meshlet_command ),
			.usage			= buffer_usage::GPU_ONLY } );
		drawCmdsIdx = dc.AllocDescriptorIdx( drawCmds );
	}

	void InitSceneDependentData( vk_context& dc, u32 instancesUpperBound )
	{
		constexpr VkBufferUsageFlags usg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		instOccludedCache = dc.CreateBuffer( {
			.name			= "Buff_InstanceOccludedCache",
			.usageFlags		= usg | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sizeInBytes	= ( ( instancesUpperBound + BitCount<u32>() - 1 ) / BitCount<u32>() ) * sizeof( u32 ),
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
		// TODO: add shader pass that clears the counters and dispatches the instance stuff
		if( !latePass )
		{
			rscTracker.UseBuffer( instOccludedCache, HT_TRANSFER_WRITE );
			rscTracker.UseBuffer( occludedMeshletsCount, HT_TRANSFER_WRITE );
		}
		rscTracker.UseBuffer( drawCount, HT_TRANSFER_WRITE );
		rscTracker.UseBuffer( visibleInstCounter, HT_TRANSFER_WRITE );
		rscTracker.UseBuffer( visibleMeshletsCount, HT_TRANSFER_WRITE );

		rscTracker.FlushBarriers( cmdBuff );

		cmdBuff.CmdFillBuffer( drawCount, 0u );
		cmdBuff.CmdFillBuffer( visibleInstCounter, 0u );
		cmdBuff.CmdFillBuffer( visibleMeshletsCount, 0u );

		if( !latePass )
		{
			cmdBuff.CmdFillBuffer( instOccludedCache, 0u );
			cmdBuff.CmdFillBuffer( occludedMeshletsCount, 0u );
		}

		rscTracker.UseBuffer( instOccludedCache, HT_COMPUTE_READWRITE );
		rscTracker.UseBuffer( occludedMeshlets, HT_COMPUTE_READWRITE );
		rscTracker.UseBuffer( occludedMeshletsCount, HT_COMPUTE_READWRITE );

		rscTracker.UseBuffer( drawCmds, HT_COMPUTE_WRITE );
		rscTracker.UseBuffer( drawCount, HT_COMPUTE_READWRITE );

		rscTracker.UseBuffer( dispatchIndirect, HT_COMPUTE_WRITE );

		rscTracker.UseBuffer( visibleInstCounter, HT_COMPUTE_WRITE );
		rscTracker.UseBuffer( visibleInstances, HT_COMPUTE_WRITE );

		rscTracker.UseBuffer( visibleMeshlets, HT_COMPUTE_WRITE );
		rscTracker.UseBuffer( visibleMeshletsCount, HT_COMPUTE_WRITE );

		rscTracker.UseBuffer( args.dbgGpuInstBuff, HT_COMPUTE_WRITE );
		rscTracker.UseBuffer( args.dbgGpuInstCountBuff, HT_COMPUTE_WRITE );

		rscTracker.UseImage( args.hiZTarget, HT_COMPUTE_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );

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
				.occludedInstCacheIdx	= instOccludedCacheIdx.slot,
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
			cmdBuff.DispatchCompute( instCullPass, pushBlock, { args.instCount, 1, 1 } );
		}
		cmdBuff.CmdPipelineMemoryBarriers( computeToComputeExecDependency );
		{
			indirect_dispatcher_params pushBlock = {
				.cullShaderWorkGrX	= instExpansionPass.groupSize.x,
				.dispatchCmdBuffIdx = dispatchIndirectIdx.slot,
				.counterBufferIdx	= visibleInstCounterIdx.slot
			};
			cmdBuff.DispatchCompute( indirectDispatchPass, pushBlock, { 1, 1, 1 } );
		}
		cmdBuff.CmdPipelineMemoryBarriers( computeToIndirectComputeExecDependency );
		{
			draw_expansion_params pushBlock = {
				.workCounterIdxConst	= visibleInstCounterIdx.slot,
				.srcBufferIdx			= visibleInstIdx.slot,
				.expandedItemsBuffIdx	= !latePass ? visibleMeshletsIdx.slot		: occludedMeshletsIdx.slot,
				.expandedItemsCountIdx	= !latePass ? visibleMeshletsCountIdx.slot	: occludedMeshletsCountIdx.slot
			};
			cmdBuff.DispatchComputeIndirect( instExpansionPass, pushBlock, dispatchIndirect );
		}
		cmdBuff.CmdPipelineMemoryBarriers( computeToIndirectComputeExecDependency );
		{
			indirect_dispatcher_params pushBlock = {
				.cullShaderWorkGrX	= clusterCullPass.groupSize.x,
				.dispatchCmdBuffIdx = dispatchIndirectIdx.slot,
				.counterBufferIdx	= !latePass ? visibleMeshletsCountIdx.slot : occludedMeshletsCountIdx.slot
			};
			cmdBuff.DispatchCompute( indirectDispatchPass, pushBlock, { 1, 1, 1 } );
		}
		cmdBuff.CmdPipelineMemoryBarriers( computeToIndirectComputeExecDependency );
		{
			meshlet_cull_params pushBlock = {
				.mltCountIdx			= !latePass ? visibleMeshletsCountIdx.slot	: occludedMeshletsCountIdx.slot,
				.expandedMltsIdx		= !latePass ? visibleMeshletsIdx.slot		: occludedMeshletsIdx.slot,

				.occludedMltBuffIdx		= !latePass ? occludedMeshletsIdx.slot		: ~u32( 0 ),
				.occludedMltCountIdx	= !latePass ? occludedMeshletsCountIdx.slot : ~u32( 0 ),

				.instDescIdx			= args.instBuffIdx.slot,
				.viewBuffIdx			= args.viewBuffIdx.slot,
				.camIdx					= args.camIdx,
				.hizTexIdx				= args.hizDesc.slot,
				.hizSamplerIdx			= args.samplerDesc.slot,
				.drawCountIdx			= drawCountIdx.slot,
				.drawCmsIdx				= drawCmdsIdx.slot,
				.isLatePass				= latePass,
			};
			cmdBuff.DispatchComputeIndirect( clusterCullPass, pushBlock, dispatchIndirect );
		}
	}
};

struct tone_mapping_pass
{
	vk_buffer					averageLuminanceBuffer;
	vk_buffer					luminanceHistogramBuffer;
	vk_buffer					atomicWgCounterBuff;

	vk_compute_pipeline			compAvgLumPipe;
	vk_compute_pipeline			compTonemapPipe;

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

		cmdBuff.CmdFillBuffer( luminanceHistogramBuffer, 0u );
		cmdBuff.CmdFillBuffer( atomicWgCounterBuff, 0u );

		rscTracker.UseBuffer( luminanceHistogramBuffer, HT_COMPUTE_READWRITE );
		rscTracker.UseBuffer( atomicWgCounterBuff, HT_COMPUTE_READWRITE );
		rscTracker.UseImage( colTarget, HT_COMPUTE_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
		rscTracker.FlushBarriers( cmdBuff );

		// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
		avg_luminance_info avgLumInfo = {
			.minLogLum = -10.0f,
			.invLogLumRange = 1.0f / 12.0f,
			.dt = dt
		};

		struct push_const
		{
			avg_luminance_info  avgLumInfo;
			u32					hdrColSrcIdx;
			u32					lumHistoIdx;
			u32					atomicWorkGrCounterIdx;
			u32					avgLumIdx;
		} pushConst = { avgLumInfo, hdrColSrcDesc.slot, lumHistoIdx.slot, atomicWgCounterIdx.slot, avgLumIdx.slot };

		cmdBuff.DispatchCompute( compAvgLumPipe, pushConst, { colTarget.width, colTarget.height, 1 } );
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

		struct push_const
		{
			u32 hdrColIdx;
			u32 sdrColIdx;
			u32 avgLumIdx;
		} pushConst = { hdrColDesc.slot, sdrColDesc.slot, avgLumIdx.slot };

		cmdBuff.DispatchCompute( compTonemapPipe, pushConst, { hdrTrgSize.x, hdrTrgSize.y, 1 } );
	}
};

struct depth_pyramid_pass
{
	vk_compute_pipeline		pipeline;
	vk_image				hzb;
	VkImageView				hzbMipViews[ MAX_MIP_LEVELS ];

	VkSampler       		quadMinSampler;
	VkSampler       		pointSampler;

	desc_hndl32     		hzbSrv;

	desc_hndl32				hzbMipUavs[ MAX_MIP_LEVELS ];
	desc_hndl32				quadMinSamplerIdx;

	desc_hndl32				pointSamplerIdx;

	void Init( vk_context& vkCtx, u16 srcWidth, u16 srcHeight )
	{
		unique_shader_ptr downsampler = vkCtx.CreateShaderFromSpirv( 
			ReadFileBinary( "bin/SpirV/compute_Pow2DownSamplerCsMain.spirv" ) );
		pipeline = vkCtx.CreateComputePipeline( *downsampler );

		u16 hzbWidth = ( u16 ) FloorPowOf2( srcWidth );
		u16 hzbHeight = ( u16 ) FloorPowOf2( srcHeight );

		constexpr VkImageUsageFlags hiZUsg =
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		image_info hzbInfo = {
			.name		= "Img_HZB",
			.format		= VK_FORMAT_R32_SFLOAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= hiZUsg,
			.width		= hzbWidth,
			.height		= hzbHeight,
			.layerCount = 1,
			.mipCount	= ( u8 ) GetImgMipCount( hzbWidth, hzbHeight )
		};

		hzb = vkCtx.CreateImage( hzbInfo );

		hzbSrv = vkCtx.AllocDescriptorIdx( { hzb.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		for( u32 mi = 0; mi < hzb.mipCount; ++mi )
		{
			hzbMipViews[ mi ] = VkMakeImgView( vkCtx.device, hzb.hndl, hzbInfo.format, mi, 1,
				VK_IMAGE_VIEW_TYPE_2D, 0, hzbInfo.layerCount );
			hzbMipUavs[ mi ] = vkCtx.AllocDescriptorIdx( { hzbMipViews[ mi ], VK_IMAGE_LAYOUT_GENERAL } );
		}

		VkSamplerReductionModeCreateInfo reduxInfo = { 
			.sType			= VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
			.reductionMode	= VK_SAMPLER_REDUCTION_MODE_MIN,
		};

		VkSamplerCreateInfo reduxSamplerCreateInfo = {
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

		quadMinSampler = vkCtx.CreateSampler( reduxSamplerCreateInfo );
		quadMinSamplerIdx = vkCtx.AllocDescriptorIdx( { quadMinSampler } );

		VkSamplerCreateInfo pointSamplerCreateInfo = {
			.sType						= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter					= VK_FILTER_NEAREST,
			.minFilter					= VK_FILTER_NEAREST,
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
		pointSampler = vkCtx.CreateSampler( pointSamplerCreateInfo );
		pointSamplerIdx = vkCtx.AllocDescriptorIdx( { pointSampler } );
	}

	void Execute( 
		vk_command_buffer&		cmdBuff,
		vk_rsc_state_tracker&	rscTracker,
		const vk_image&			depthTarget,
		desc_hndl32				depthIdx
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "HZB Multi Pass", {} );

		rscTracker.UseImage( depthTarget, HT_COMPUTE_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
		rscTracker.UseImage( hzb, HT_COMPUTE_WRITE, VK_IMAGE_LAYOUT_GENERAL );

		rscTracker.FlushBarriers( cmdBuff );

		// TODO: do we need to mem flush here ?
		VkMemoryBarrier2 executionBarrier[] = { {
				.sType			= VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask	= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			    .dstStageMask	= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		} };

		u32 mipLevel = 0;
		u32 srcImg = depthIdx.slot;
		u32 srcWidth = depthTarget.width;
		u32 srcHeight = depthTarget.height;
		for( u32 mi = 0; mi < hzb.mipCount; ++mi )
		{
			[[unlikely]]
			if( mi > 0 )
			{
				mipLevel = mi - 1;
				srcImg = hzbSrv.slot;
			}

			u32 levelWidth = std::max( 1u, u32( hzb.width ) >> mi );
			u32 levelHeight = std::max( 1u, u32( hzb.height ) >> mi );

			multi_pass_downsampler_params pushConst = {
				.srcSize				= { srcWidth, srcHeight },
				.dstSize				= { levelWidth, levelHeight },
				.reductionSamplerIdx	= quadMinSamplerIdx.slot,
				.pointSamplerIdx		= pointSamplerIdx.slot,
				.inImgIdx				= srcImg,
				.inImgLod				= mipLevel,
				.outImgIdx				= hzbMipUavs[ mi ].slot,
				.isMip0FromNonPot		= u32( 0 == mi )
			};

			cmdBuff.DispatchCompute( pipeline, pushConst, { levelWidth, levelHeight, 1 } );
			cmdBuff.CmdPipelineMemoryBarriers( executionBarrier );

			srcWidth = levelWidth;
			srcHeight = levelHeight;
		}

		// TODO: del ?
		rscTracker.UseImage( depthTarget,
			{  HT_DEPTH_ATTACHMENT_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT },
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscTracker.FlushBarriers( cmdBuff );
	}
};

struct vbuffer_pass_args
{
	const vk_image&			depthTarget;
	const vk_buffer&      	indexBuff;
	VkIndexType           	indexType;
	const vk_buffer&		drawCmds;
	const vk_buffer&		drawCount;
	desc_hndl32				drawBuffIdx;
	desc_hndl32				instBuffIdx;
	desc_hndl32				camIdx;
};

struct vbuffer_pass
{
	static constexpr VkFormat	VBUFF_FORMAT = VK_FORMAT_R32G32_UINT;

	vk_image					vbuffRG32Target;

	VkPipeline					gfxVBuffPipeline;
	vk_compute_pipeline			compDbgHashTriToScPipeline;

	desc_hndl32					vbuffRG32Srv;

	void Init( vk_context& dc, VkFormat depthFormat, u16 width, u16 height )
	{
		vbuffRG32Target = dc.CreateImage( {
			.name		= "Img_VBufferTarget",
			.format		= VBUFF_FORMAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.width		= width,
			.height		= height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		vbuffRG32Srv = dc.AllocDescriptorIdx( { vbuffRG32Target.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		{
			unique_shader_ptr vtx = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/vertex_VBufferVsMain.spirv" ) );
			unique_shader_ptr frag = dc.CreateShaderFromSpirv( ReadFileBinary( "bin/SpirV/pixel_VBufferPsMain.spirv" ) );

			vk_gfx_pso_config vbuffState = {
				.polyMode			= VK_POLYGON_MODE_FILL,
				.cullFlags			= HT_CULL_MODE,
				.frontFace			= HT_FRONT_FACE,
				.primTopology		= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				.depthCompareOp		= VK_COMPARE_OP_GREATER,
				.depthWrite			= true,
				.depthTestEnable	= true,
				.blendCol			= false
			};

			vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

			gfxVBuffPipeline = dc.CreateGfxPipeline( shaderStages, dynamicStates, &VBUFF_FORMAT,
				1, depthFormat, vbuffState, dc.globalPipelineLayout );
		}
		{
			unique_shader_ptr comp = dc.CreateShaderFromSpirv(
				ReadFileBinary( "bin/SpirV/compute_VBufferDbgDrawCsMain.spirv" ) );
			compDbgHashTriToScPipeline = dc.CreateComputePipeline( *comp );
		}
	}

	void DrawIndexedIndirect(
		vk_command_buffer&			cmdBuff,
		vk_rsc_state_tracker&		rscTracker,
		const vbuffer_pass_args&	args,
		bool						latePass
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "VBuffer Pass", {} );

		rscTracker.UseImage( args.depthTarget, HT_DEPTH_TARGET_FRAG_TESTS_WRITE, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscTracker.UseImage( vbuffRG32Target, HT_COLOR_TARGET_OUT_WRITE, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );

		rscTracker.UseBuffer( args.drawCmds, HT_DRAW_INDIRECT_READ );
		rscTracker.UseBuffer( args.drawCount, HT_DRAW_INDIRECT_READ );
		rscTracker.FlushBarriers( cmdBuff );

		// NOTE: since we do this incrementally we want the 2nd pass to just load
		const VkAttachmentLoadOp loadOp = latePass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;

		VkClearValue clear = { .color = GetVBufferClearValue() };

		VkRenderingAttachmentInfo attInfos[] = {
			VkMakeAttachmentInfo( vbuffRG32Target.view, loadOp, VK_ATTACHMENT_STORE_OP_STORE, clear )
		};
		VkRenderingAttachmentInfo depthWrite = VkMakeAttachmentInfo( args.depthTarget.view, loadOp,
			VK_ATTACHMENT_STORE_OP_STORE, { .depthStencil = REV_Z_DEPTH_BUFFER_CLEAR_VAL } );

		vk_rendering_info renderingInfo = {
			.colorAttachments	= attInfos,
			.pDepthAttachment	= &depthWrite,
			.viewport			= VkCorrectedGetViewport( vbuffRG32Target.width, vbuffRG32Target.height ),
			.scissor			= VkGetScissor( vbuffRG32Target.width, vbuffRG32Target.height )
		};

		vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

		cmdBuff.CmdBindPipelineAndBindlessDesc( gfxVBuffPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS );

		vbuffer_params pushBlock = {
			.drawBuffIdx	= args.drawBuffIdx.slot,
			.instBuffIdx	= args.instBuffIdx.slot,
			.camIdx			= args.camIdx.slot
		};
		cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
		cmdBuff.CmdDrawIndexedIndirectCount<draw_meshlet_command>( args.indexBuff, args.indexType,
			args.drawCmds, args.drawCount );
	}

	void DebugDrawHashedVBuffer(
		vk_command_buffer&		cmdBuff,
		vk_rsc_state_tracker&	rscTracker,
		const vk_image&			dstImg,
		desc_hndl32				dstImgIdx
	) {
		HT_ASSERT( ( vbuffRG32Target.width == dstImg.width ) && ( vbuffRG32Target.height == dstImg.height ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "VBuffer Dbg Tri hash Pass", {} );

		rscTracker.UseImage( dstImg, HT_COMPUTE_WRITE, VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.UseImage( vbuffRG32Target, HT_COMPUTE_READ, VK_IMAGE_LAYOUT_GENERAL );
		rscTracker.FlushBarriers( cmdBuff );

		vbuffer_dbg_draw_params pushBlock = { .srcIdx = vbuffRG32Srv.slot, .dstIdx = dstImgIdx.slot };
		cmdBuff.DispatchCompute( compDbgHashTriToScPipeline, pushBlock,
			{ vbuffRG32Target.width, vbuffRG32Target.height, 1 } );
	}
};

struct fwd_pass_args
{
	const vk_image&			colorTarget;
	const vk_image&			depthTarget;
	const vk_buffer&      	indexBuff;
	VkIndexType           	indexType;
	const vk_buffer&		drawCmds;
	const vk_buffer&		drawCount;
	desc_hndl32				drawBuffIdx;
	desc_hndl32				instBuffIdx;
	desc_hndl32				camIdx;
};

struct fwd_pass
{
	VkPipeline gfxDepthPrepass;
	VkPipeline gfxLambertianClay;

	void Init( vk_context& dc, VkFormat depthFormat, VkFormat colorFormat )
	{
		{
			unique_shader_ptr vtx = dc.CreateShaderFromSpirv( ReadFileBinary(
				"bin/SpirV/vertex_DepthPrepassVsMain.spirv" ) );

			vk_gfx_pso_config depthPrepassState = {
				.polyMode			= VK_POLYGON_MODE_FILL,
				.cullFlags			= HT_CULL_MODE,
				.frontFace			= HT_FRONT_FACE,
				.primTopology		= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				.depthCompareOp		= VK_COMPARE_OP_GREATER,
				.depthWrite			= true,
				.depthTestEnable	= true,
				.blendCol			= false
			};

			vk_gfx_shader_stage shaderStages[] = { *vtx };
			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

			gfxDepthPrepass = dc.CreateGfxPipeline( shaderStages, dynamicStates, 0,
				0, depthFormat, depthPrepassState, dc.globalPipelineLayout );
		}
		{
			unique_shader_ptr vtx = dc.CreateShaderFromSpirv( ReadFileBinary(
				"bin/SpirV/vertex_MeshletPassVsMain.spirv" ) );
			unique_shader_ptr frag = dc.CreateShaderFromSpirv( ReadFileBinary(
				"bin/SpirV/pixel_MeshletClayPassPsMain.spirv" ) );

			vk_gfx_pso_config gfxState = {
				.polyMode				= VK_POLYGON_MODE_FILL,
				.cullFlags				= HT_CULL_MODE,
				.frontFace				= HT_FRONT_FACE,
				.primTopology			= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				.srcColorBlendFactor	= VK_BLEND_FACTOR_SRC_ALPHA,
				.dstColorBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				.colorBlendOp			= VK_BLEND_OP_ADD,
				.depthCompareOp			= VK_COMPARE_OP_GREATER_OR_EQUAL, // NOTE: OR_EQ bc we basically draw the same geom again
				.depthWrite				= false,
				.depthTestEnable		= true
			};

			vk_gfx_shader_stage shaderStages[] = { *vtx, *frag };
			VkDynamicState dynamicStates[] = {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
				//VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
				//VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
				VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT
			};

			gfxLambertianClay = dc.CreateGfxPipeline( shaderStages, dynamicStates, &colorFormat,
				1, depthFormat, gfxState, dc.globalPipelineLayout );
		}
	}

	void DrawIndexedIndirect(
		vk_command_buffer&		cmdBuff,
		vk_rsc_state_tracker&	rscTracker,
		const fwd_pass_args&	args,
		bool					latePass,
		bool					isXRayOn
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Depth_Prepass + Fwd/Transparency Pass", {} );

		rscTracker.UseImage( args.depthTarget, HT_DEPTH_TARGET_FRAG_TESTS_WRITE, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscTracker.UseImage( args.colorTarget, HT_COLOR_TARGET_OUT_WRITE, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );

		rscTracker.UseBuffer( args.drawCmds, HT_DRAW_INDIRECT_READ );
		rscTracker.UseBuffer( args.drawCount, HT_DRAW_INDIRECT_READ );
		rscTracker.FlushBarriers( cmdBuff );

		// NOTE: since we do this incrementally we want the 2nd pass to just load
		const VkAttachmentLoadOp loadOp = latePass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;

		{
			VkRenderingAttachmentInfo depthWrite = VkMakeAttachmentInfo( args.depthTarget.view, loadOp,
				VK_ATTACHMENT_STORE_OP_STORE, { .depthStencil = REV_Z_DEPTH_BUFFER_CLEAR_VAL } );

			vk_rendering_info renderingInfo = {
				.pDepthAttachment	= &depthWrite,
				.viewport			= VkCorrectedGetViewport( args.colorTarget.width, args.colorTarget.height ),
				.scissor			= VkGetScissor( args.colorTarget.width, args.colorTarget.height )
			};

			vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

			cmdBuff.CmdBindPipelineAndBindlessDesc( gfxDepthPrepass, VK_PIPELINE_BIND_POINT_GRAPHICS );

			depth_prepass_params pushBlock = {
				.drawBuffIdx	= args.drawBuffIdx.slot,
				.instBuffIdx	= args.instBuffIdx.slot,
				.camIdx			= args.camIdx.slot
			};
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDrawIndexedIndirectCount<draw_meshlet_command>( args.indexBuff, args.indexType,
				args.drawCmds, args.drawCount );
		}
		rscTracker.UseImage( args.depthTarget, HT_DEPTH_TARGET_FRAG_TESTS_READ, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
		rscTracker.FlushBarriers( cmdBuff );
		{
			VkRenderingAttachmentInfo attInfos[] = {
				VkMakeAttachmentInfo( args.colorTarget.view, loadOp, VK_ATTACHMENT_STORE_OP_STORE, {} )
			};
			VkRenderingAttachmentInfo depthRead = VkMakeAttachmentInfo( args.depthTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD,
				VK_ATTACHMENT_STORE_OP_NONE, {} );

			vk_rendering_info renderingInfo = {
				.colorAttachments	= attInfos,
				.pDepthAttachment	= &depthRead,
				.viewport			= VkCorrectedGetViewport( args.colorTarget.width, args.colorTarget.height ),
				.scissor			= VkGetScissor( args.colorTarget.width, args.colorTarget.height )
			};

			vk_gfx_dynamic_state dynamicState = {
				.colorBlendDynamicStateOn	= true,
				.colorBlendEnabled			= isXRayOn
			};

			vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo, &dynamicState );

			cmdBuff.CmdBindPipelineAndBindlessDesc( gfxLambertianClay, VK_PIPELINE_BIND_POINT_GRAPHICS );

			meshlet_pass_params pushBlock = {
				.drawBuffIdx	= args.drawBuffIdx.slot,
				.instBuffIdx	= args.instBuffIdx.slot,
				.camIdx			= args.camIdx.slot
			};
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDrawIndexedIndirectCount<draw_meshlet_command>( args.indexBuff, args.indexType,
				args.drawCmds, args.drawCount );
		}
	}
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
	depth_pyramid_pass						hzbPass;
	tone_mapping_pass						tonemappingPass;
	debug_draw_passes						dbgPass;
	vbuffer_pass							vBuffPass;
	fwd_pass								fwdPass;
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

	vk_image								depthTarget;
	vk_image								colorTarget;

	offset_allocator_t						meshletAllocator;
	offset_allocator_t						vtxAllocator;
	offset_allocator_t						triAllocator;

	u64										vFrameIdx = 0;

	VkSampler								pbrSampler;
	desc_hndl32								pbrSamplerIdx;

	desc_hndl32								globalDataIdx;

	desc_hndl32								depthSrv;
	desc_hndl32								colorSrv;
	desc_hndl32								colorUav;

	const u32								framesInFlight = MAX_FIF;


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

	u32 /* numValidInstances */ UpdateSceneData( const virtual_frame& thisVFrame, const frame_data& frameData );

	inline void CreateGlobalTargets( u16 width, u16 height )
	{
		depthTarget = pVkCtx->CreateImage( {
			.name		= "Img_DepthTarget",
			.format		= config.DEPTH_FORMAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.width		= width,
			.height		= height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		depthSrv = pVkCtx->AllocDescriptorIdx( { depthTarget.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		constexpr VkImageUsageFlags colorUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		colorTarget = pVkCtx->CreateImage( {
			.name		= "Img_ColorTarget",
			.format		= config.desiredColorFormat,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= colorUsageFlags,
			.width		= width,
			.height		= height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		colorSrv = pVkCtx->AllocDescriptorIdx( { colorTarget.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
		colorUav = pVkCtx->AllocDescriptorIdx( { colorTarget.view, VK_IMAGE_LAYOUT_GENERAL } );
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
	tonemappingPass.Init( *pVkCtx );
	dbgPass.Init( *pVkCtx, config );
	fwdPass.Init( *pVkCtx, config.DEPTH_FORMAT, config.desiredColorFormat );

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
			dstOffsetInBytes, payloadSizeInBytes, pVkCtx->copyQueue.familyIdx,
			pVkCtx->gfxQueue.familyIdx ) );
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

u32 renderer_context::UpdateSceneData( const virtual_frame& thisVFrame, const frame_data& frameData )
{
	HT_ASSERT( BYTE_COUNT( frameData.views ) <= thisVFrame.viewData.sizeInBytes );
	std::memcpy( thisVFrame.viewData.hostVisible, std::data( frameData.views ), BYTE_COUNT( frameData.views ) );

	ht_stretchybuff<gpu_mesh> gpuMeshTable = HtNewStretchyBuffFromMem<gpu_mesh>(
		thisVFrame.gpuMeshTable.hostVisible, thisVFrame.gpuMeshTable.sizeInBytes );
	// NOTE: for now we alloc for worst scenario and copy it with invalid slots too, those won't be accessed anyways
	HT_ASSERT( std::size( rendererComponents ) <= gpuMeshTable.capacity() );

	for( const ht_mesh_component& component : rendererComponents.items )
	{
		gpuMeshTable.push_back( component.desc );
	}

	ht_stretchybuff<gpu_instance> gpuInstList = HtNewStretchyBuffFromMem<gpu_instance>(
		thisVFrame.gpuInstances.hostVisible, thisVFrame.gpuInstances.sizeInBytes );
	HT_ASSERT( std::size( frameData.instances ) <= gpuInstList.capacity() );

	for( const instance_desc& sceneNode : frameData.instances )
	{
		mesh_hndl32 hMesh = std::bit_cast<mesh_hndl32>( sceneNode.meshIdx );
		gpuInstList.push_back( {
			.toWorld = TrsToFloat4x3RowMaj( sceneNode.transform ),
			.meshIdx = hMesh.slotIdx
		} );
	}

	return ( u32 ) std::size( gpuInstList );
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

	VkResult timelineWaitResult = pVkCtx->TimelineTryWaitFor( pVkCtx->gpuFrameTimeline, framesInFlight,
		UINT64_MAX );
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

		CreateGlobalTargets( config.renderWidth, config.renderHeight );
		rscStateTracker.UseImage( depthTarget, {}, VK_IMAGE_LAYOUT_UNDEFINED );
		rscStateTracker.UseImage( colorTarget, {}, VK_IMAGE_LAYOUT_UNDEFINED );

		vBuffPass.Init( *pVkCtx, depthTarget.format, config.renderWidth, config.renderHeight );

		rscStateTracker.UseImage( vBuffPass.vbuffRG32Target, {}, VK_IMAGE_LAYOUT_UNDEFINED );

		hzbPass.Init( *pVkCtx, depthTarget.width, depthTarget.height );

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

		rscStateTracker.UseBuffer( tonemappingPass.averageLuminanceBuffer, HT_TRANSFER_WRITE );

		thisFrameCmdBuffer.CmdFillBuffer( tonemappingPass.averageLuminanceBuffer, 0u );

		rscStateTracker.UseBuffer( tonemappingPass.averageLuminanceBuffer, HT_COMPUTE_READWRITE );

		rscStateTracker.UseImage( hzbPass.hzb, {}, VK_IMAGE_LAYOUT_UNDEFINED );

		rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

		initResources = true;
	}

	pVkCtx->FlushPendingDescriptorUpdates();


	dbgPass.ResetDrawCounters( thisFrameCmdBuffer, rscStateTracker );

	const culling_pass_args cullPassArgs = {
		.dbgGpuInstBuff			= dbgPass.gpuInstBuff,
		.dbgGpuInstCountBuff	= dbgPass.gpuInstCountBuff,
		.hiZTarget				= hzbPass.hzb,
		.instCount				= instCount,
		.instBuffIdx			= thisVFrame.instDesc,
		.meshTableIdx			= thisVFrame.gpuMeshTableDesc,
		.viewBuffIdx			= thisVFrame.viewDataIdx,
		.camIdx					= !frameData.dbgDrawFlags.freezeMainView ? 0u : 1u, // TODO: don't hardcode here
		.hizDesc				= hzbPass.hzbSrv,
		.samplerDesc			= hzbPass.quadMinSamplerIdx,
		.dbgGpuInstBuffIdx		= dbgPass.gpuInstBuffIdx,
		.dbgGpuInstCountBuffIdx = dbgPass.gpuInstCountBuffIdx
	};
	cullingPass.Execute( thisFrameCmdBuffer, rscStateTracker, cullPassArgs, false );

	const vbuffer_pass_args vbuffPassArgs = {
		.depthTarget			= depthTarget,
		.indexBuff 				= megaGpuTriBuff,
		.indexType 				= VK_INDEX_TYPE_UINT8,
		.drawCmds				= cullingPass.drawCmds,
		.drawCount				= cullingPass.drawCount,
		.drawBuffIdx			= cullingPass.drawCmdsIdx,
		.instBuffIdx			= thisVFrame.instDesc,
		.camIdx					= thisVFrame.viewDataIdx
	};
	//vBuffPass.DrawIndexedIndirect( thisFrameCmdBuffer, rscStateTracker, vbuffPassArgs, false );

	const fwd_pass_args fwdPassArgs = {
		.colorTarget			= colorTarget,
		.depthTarget			= depthTarget,
		.indexBuff 				= megaGpuTriBuff,
		.indexType 				= VK_INDEX_TYPE_UINT8,
		.drawCmds				= cullingPass.drawCmds,
		.drawCount				= cullingPass.drawCount,
		.drawBuffIdx			= cullingPass.drawCmdsIdx,
		.instBuffIdx			= thisVFrame.instDesc,
		.camIdx					= thisVFrame.viewDataIdx
	};
	fwdPass.DrawIndexedIndirect( thisFrameCmdBuffer, rscStateTracker, fwdPassArgs, false, frameData.dbgDrawFlags.drawXRayMode );

	hzbPass.Execute( thisFrameCmdBuffer, rscStateTracker, depthTarget, depthSrv );

	cullingPass.Execute( thisFrameCmdBuffer, rscStateTracker, cullPassArgs, true );

	fwdPass.DrawIndexedIndirect( thisFrameCmdBuffer, rscStateTracker, fwdPassArgs, true, frameData.dbgDrawFlags.drawXRayMode );
	//vBuffPass.DrawIndexedIndirect( thisFrameCmdBuffer, rscStateTracker, vbuffPassArgs, true );

	hzbPass.Execute( thisFrameCmdBuffer, rscStateTracker, depthTarget, depthSrv );

	[[unlikely]]
	if( !frameData.dbgDrawFlags.vBuffPixelHash )
	{
		//dbgPass.DrawAsLamberitanClay( thisFrameCmdBuffer, rscStateTracker, vBuffPass.vbuffRG32Target,
		//	colorTarget, vBuffPass.vbuffRG32Srv, colorUav, thisVFrame.instDesc,
		//	thisVFrame.gpuMeshTableDesc, thisVFrame.viewDataIdx );

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
		//vBuffPass.DebugDrawHashedVBuffer( thisFrameCmdBuffer, rscStateTracker, colorTarget, colorUav );
	}

	[[unlikely]]
	if( frameData.dbgDrawFlags.dbgDraw )
	{
		dbgPass.DrawWireframesGPU( thisFrameCmdBuffer, rscStateTracker, colorTarget, thisVFrame.viewDataIdx );
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
		dbgPass.DrawWireframeCPU( thisFrameCmdBuffer, rscStateTracker, colorTarget, thisVFrame.viewDataIdx );
	}

	rscStateTracker.UseImage( colorTarget, HT_COLOR_TARGET_OUT_READWRITE, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
	rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

	imguiPass.DrawUiPass( *pVkCtx, thisFrameCmdBuffer.hndl, colorTarget, currentFrameIdx, currentFrameInFlightIdx );

	// NOTE: init swapchian
	u32 scImgIdx = pVkCtx->AcquireNextSwapchainImageBlocking( thisVFrame.canGetImgSema );
	const vk_swapchain_image& scImg = pVkCtx->scImgs[ scImgIdx ];

	// NOTE: we need an exec dependency between AcquireNextSwapchainImageBlocking and the compute write
	 constexpr VkPipelineStageFlags2 execDep =
	 	VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	rscStateTracker.UseImage( scImg.img, { 0, execDep }, VK_IMAGE_LAYOUT_UNDEFINED );
	rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

	rscStateTracker.UseImage( colorTarget, HT_TRANSFER_READ, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL  );
	rscStateTracker.UseImage( scImg.img, HT_TRANSFER_WRITE, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL  );
	rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

	thisFrameCmdBuffer.CmdCopyImageSameProps( colorTarget, scImg.img );

	rscStateTracker.UseImage( scImg.img, { 0, 0 }, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
	rscStateTracker.FlushBarriers( thisFrameCmdBuffer );

	// NOTE: remove sc image to avoid handling this logic inside the tracker
	rscStateTracker.StopTrackingResource( ( u64 ) scImg.img.hndl );

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
