#ifndef __VK_TYPES_H__
#define __VK_TYPES_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "ht_core_types.h"
#include "ht_fixed_string.h"

struct vk_shader
{
	fixed_string<128>		entryPoint;
	VkShaderModule			module;
	VkShaderStageFlagBits	stage;
	u32x3					groupSize; // NOTE: only for compute
};

struct vk_compute_pipeline
{
	VkPipeline	hndl;
	u32x3		groupSize;
};

struct vk_gfx_shader_stage : VkPipelineShaderStageCreateInfo
{
	inline vk_gfx_shader_stage( const vk_shader& shader ) : VkPipelineShaderStageCreateInfo{}
	{
		this->sType		= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		this->stage		= shader.stage;
		this->module	= shader.module;
		this->pName		= std::data( shader.entryPoint );
	}
};

struct vk_gfx_pso_config
{
	VkPolygonMode		polyMode;
	VkCullModeFlags		cullFlags;
	VkFrontFace			frontFace;
	VkPrimitiveTopology primTopology;
	VkBlendFactor       srcColorBlendFactor;
	VkBlendFactor       dstColorBlendFactor;
	VkBlendOp           colorBlendOp;
	VkBlendFactor       srcAlphaBlendFactor;
	VkBlendFactor       dstAlphaBlendFactor;
	VkBlendOp           alphaBlendOp;
	VkCompareOp			depthCompareOp;
	bool				depthWrite;
	bool				depthTestEnable;
	bool				blendCol;
};

constexpr vk_gfx_pso_config DEFAULT_PSO = {
	.polyMode				= VK_POLYGON_MODE_FILL,
	.cullFlags				= VK_CULL_MODE_BACK_BIT,
	.frontFace				= VK_FRONT_FACE_COUNTER_CLOCKWISE,
	.primTopology			= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	.srcColorBlendFactor	= VK_BLEND_FACTOR_ONE,
	.dstColorBlendFactor	= VK_BLEND_FACTOR_ONE,
	.colorBlendOp			= VK_BLEND_OP_ADD,
	.srcAlphaBlendFactor	= VK_BLEND_FACTOR_ZERO,
	.dstAlphaBlendFactor 	= VK_BLEND_FACTOR_ZERO,
	.alphaBlendOp 			= VK_BLEND_OP_ADD,
	.depthCompareOp			= VK_COMPARE_OP_GREATER,
	.depthWrite 			= true,
	.depthTestEnable		= true,
	.blendCol				= false
};

struct vk_swapchain_config
{
	VkFormat			format		= VK_FORMAT_B8G8R8A8_UNORM;
	VkImageUsageFlags	imgUsage	= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	VkPresentModeKHR	presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
	u32					minNumImgs	= 3;
};

// TODO:
struct vk_renderer_config
{
	static constexpr u64		MAX_FRAMES_IN_FLIGHT_ALLOWED	= 2;
	static constexpr u64		MAX_DESCRIPTOR_COUNT_PER_TYPE	= u16( -1 );
	// TODO: we only need this if we do reverse Z
	static constexpr VkFormat	DEPTH_FORMAT					= VK_FORMAT_D32_SFLOAT;

	vk_swapchain_config		scConfig			= {};

	VkFormat				desiredColorFormat	= VK_FORMAT_B8G8R8A8_UNORM; // NOTE: for now use same as SC //VK_FORMAT_R16G16B16A16_SFLOAT;
	VkFormat				desiredHiZFormat	= VK_FORMAT_R32_SFLOAT;
	u16             		renderWidth;
	u16             		renderHeight;
};

enum vk_desc_binding_t : u32
{
	SAMPLER = 0,
	STORAGE_BUFFER,
	STORAGE_IMAGE,
	SAMPLED_IMAGE,
	COUNT
};

#endif // !__VK_TYPES_H__

