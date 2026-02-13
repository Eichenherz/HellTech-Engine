#ifndef __VK_SWAPCHAIN_H__
#define __VK_SWAPCHAIN_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>

#include <array>

#include "vk_procs.h"

#include "vk_error.h"
#include "core_types.h"

constexpr u32 VK_SWAPCHAIN_MAX_IMG_ALLOWED = 3;

struct vk_swapchain_image
{
	VkImage			hndl;
	VkImageView		view;
	VkSemaphore		canPresentSema;
};

struct vk_swapchain
{
	std::array<vk_swapchain_image, VK_SWAPCHAIN_MAX_IMG_ALLOWED> imgs;
	VkSwapchainKHR		swapchain;
	VkFormat			imgFormat;
	u16					width;
	u16					height;
	u8					imgCount;
};

// TODO: more params ??
inline VkViewport VkGetSwapchainViewport( const vk_swapchain& sc )
{
	return { 0.0f, ( float ) sc.height, ( float ) sc.width, -( float ) sc.height, 0.0f, 1.0f };
}

inline VkRect2D VkGetSwapchianScissor( const vk_swapchain& sc )
{
	return { { 0, 0 }, { sc.width, sc.height } };
}

// TODO: sep initial validation form sc creation when resize ?
inline vk_swapchain
VkMakeSwapchain(
	VkDevice			vkDevice,
	VkPhysicalDevice	vkPhysicalDevice,
	VkSurfaceKHR		vkSurf,
	u32					queueFamIdx,
	VkFormat			scDesiredFormat,
	u32                 numImages
) {
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VK_CHECK( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkPhysicalDevice, vkSurf, &surfaceCaps ) );
	HT_ASSERT( surfaceCaps.maxImageArrayLayers >= 1 );

	u32 scImgCount = numImages;
	HT_ASSERT( ( scImgCount > surfaceCaps.minImageCount ) && ( scImgCount < surfaceCaps.maxImageCount ) );
	HT_ASSERT( ( surfaceCaps.currentExtent.width <= surfaceCaps.maxImageExtent.width ) &&
			   ( surfaceCaps.currentExtent.height <= surfaceCaps.maxImageExtent.height ) );

	VkCompositeAlphaFlagBitsKHR surfaceComposite =
		( surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR )
		? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
		: ( surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR )
		? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
		: ( surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR )
		? VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
		: VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

	VkSurfaceFormatKHR scFormatAndColSpace = {};
	{
		u32 formatCount = 0;
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( vkPhysicalDevice, vkSurf, &formatCount, 0 ) );
		std::vector<VkSurfaceFormatKHR> formats( formatCount );
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( vkPhysicalDevice, vkSurf, &formatCount, std::data( formats ) ) );

		for( u64 i = 0; i < formatCount; ++i )
		{
			if( formats[ i ].format == scDesiredFormat )
			{
				scFormatAndColSpace = formats[ i ];
				break;
			}
		}
		HT_ASSERT( scFormatAndColSpace.format );
	}

	VkPresentModeKHR presentMode = VkPresentModeKHR( 0 );
	{
		u32 numPresentModes;
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, 0 ) );
		std::vector<VkPresentModeKHR> presentModes( numPresentModes );
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( 
			vkPhysicalDevice, vkSurf, &numPresentModes, std::data( presentModes ) ) );

		constexpr VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		for( u32 j = 0; j < numPresentModes; ++j )
		{
			if( presentModes[ j ] == desiredPresentMode )
			{
				presentMode = desiredPresentMode;
				break;
			}
		}
		HT_ASSERT( presentMode );
	}

	VkImageUsageFlags scImgUsage =
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	HT_ASSERT( ( surfaceCaps.supportedUsageFlags & scImgUsage ) == scImgUsage );


	VkSwapchainCreateInfoKHR scInfo = { 
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = vkSurf,
		.minImageCount = scImgCount,
		.imageFormat = scFormatAndColSpace.format,
		.imageColorSpace = scFormatAndColSpace.colorSpace,
		.imageExtent = surfaceCaps.currentExtent,
		.imageArrayLayers = 1,
		.imageUsage = scImgUsage,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &queueFamIdx,
		.preTransform = surfaceCaps.currentTransform,
		.compositeAlpha = surfaceComposite,
		.presentMode = presentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = 0
	};

	VkImageFormatProperties scImageProps = {};
	VK_CHECK( vkGetPhysicalDeviceImageFormatProperties( 
		vkPhysicalDevice, scInfo.imageFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, scInfo.imageUsage,
		scInfo.flags, &scImageProps ) );

	VkSwapchainKHR swapchain;
	VK_CHECK( vkCreateSwapchainKHR( vkDevice, &scInfo, 0, &swapchain ) );

	u32 scImgsNum = 0;
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, swapchain, &scImgsNum, 0 ) ); 
	HT_ASSERT( scImgsNum == scInfo.minImageCount );

	std::vector<VkImage> vkScImgs( scImgsNum );
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, swapchain, &scImgsNum, std::data( vkScImgs ) ) );

	VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( scInfo.imageFormat );

	// TODO: where to hardcode this
	constexpr u64 scImageCount = sizeof( vk_swapchain::imgs ) / sizeof( vk_swapchain_image );
	HT_ASSERT( scImgsNum == scImageCount );
	std::array<vk_swapchain_image, scImageCount> scImgs;

	for( u64 scii = 0; scii < scImgsNum; ++scii )
	{
		VkImage img = vkScImgs[ scii ];

		// NOTE: yeah yeah, multiple allocs, this is fine for init !
		std::string name = std::format( "Img_Swapchain{}", scii );
		VkDbgNameObj( img, vkDevice, name.c_str() );

		VkImageView view = VkMakeImgView( vkDevice, img, scInfo.imageFormat, 0, 1, VK_IMAGE_VIEW_TYPE_2D, 0, 1 );

		VkSemaphoreCreateInfo semaInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VkSemaphore canPresentSema;
		VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &canPresentSema ) );

		scImgs[ scii ] = {
			.hndl = img,
			.view = view,
			.canPresentSema = canPresentSema
		};
	}
	
	HT_ASSERT( ( scInfo.imageExtent.width < u32( u16( -1 ) ) ) && ( scInfo.imageExtent.height < u32( u16( -1 ) ) ) );

	return {
		.imgs = scImgs,
		.swapchain = swapchain,
		.imgFormat = scInfo.imageFormat,
		.width = ( u16 ) scInfo.imageExtent.width,
		.height = ( u16 ) scInfo.imageExtent.height,
		.imgCount = ( u8 ) scInfo.minImageCount
	};
}

inline VkSurfaceKHR VkMakeWinSurface( VkInstance vkInst, HINSTANCE hInst, HWND hWnd )
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
	VkWin32SurfaceCreateInfoKHR surfInfo = { 
		.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
		.hinstance = hInst,
		.hwnd = hWnd,
	};

	VkSurfaceKHR vkSurf;
	VK_CHECK( vkCreateWin32SurfaceKHR( vkInst, &surfInfo, 0, &vkSurf ) );
	return vkSurf;

#else
#error Must provide OS specific Surface
#endif // VK_USE_PLATFORM_WIN32_KHR
}



#endif // !__VK_SWAPCHAIN_H__
