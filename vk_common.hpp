#ifndef __VK_COMMON__
#define __VK_COMMON__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
// TODO: header + .cpp ?
// TODO: revisit this
#include "vk_procs.h"

#include "core_types.h"

constexpr u32 VK_API_VERSION = VK_API_VERSION_1_3;
constexpr u32 APP_VERSION = 1;

// TODO: enable gfx debug outside of VS Debug
constexpr bool vkValidationLayerFeatures = 1;

#endif // !__VK_COMMON__

