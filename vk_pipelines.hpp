#pragma once

#include "vk_common.hpp"

#include "sys_os_api.h"

#include "vk_utils.hpp"
#include "vk_shaders.hpp"

#include <vector>

static constexpr bool colorBlending = 0;

// TODO: add more ?
constexpr VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

constexpr VkPrimitiveTopology DEFAULT_PRIMITIVE_TOPOLOGY = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

struct vk_rasterization_config
{
	float               extraPrimitiveOverestimationSize;
	bool                conservativeRasterEnable;
	VkPolygonMode		polygonMode;
	VkCullModeFlags		cullFlags;
	VkFrontFace			frontFace;
};

inline constexpr vk_rasterization_config DEFAULT_RASTER_CONFIG = {
	.extraPrimitiveOverestimationSize = 0.0f,
	.conservativeRasterEnable = false,
	.polygonMode = VK_POLYGON_MODE_FILL,
	.cullFlags = VK_CULL_MODE_BACK_BIT,
	.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
};

struct vk_depth_stencil_config
{
	bool				depthWrite;
	bool				depthTestEnable;
};

static_assert( sizeof( vk_depth_stencil_config ) <= 8u );

inline constexpr vk_depth_stencil_config DEFAULT_DEPTH_STENCIL_CONFIG = {
	.depthWrite = true,
	.depthTestEnable = true,
};

struct vk_color_blending_config
{
	VkBlendFactor       srcColorBlendFactor;
	VkBlendFactor       dstColorBlendFactor;
	VkBlendFactor       srcAlphaBlendFactor;
	VkBlendFactor       dstAlphaBlendFactor;
};

inline constexpr vk_color_blending_config DEFAULT_COLOR_BLENDING = {
	.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
	.dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
	.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
	.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
};