#ifndef __VK_FRAME_GRAPH__
#define __VK_FRAME_GRAPH__

#include "core_types.h"
#include "vk_resources.hpp"
#include <vector>

// TODO: proper handle ?
using resource_handle = u16; 

struct render_pass
{
	std::vector<resource_handle> resources;
};

struct vk_resource_registry
{
	std::vector<vk_buffer> buffers;
	std::vector<vk_image> images;
	std::vector<VkSampler> samplers;
};

struct frame_graph
{
	std::vector<render_pass> passes;
	// barriers
	
	template<typename T>
	void AddPassCallback( const char* name, T callback );
};

#endif // !__VK_FRAME_GRAPH__

