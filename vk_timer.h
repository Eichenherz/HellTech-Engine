#ifndef __VK_TIMER_H__
#define __VK_TIMER_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>
#include "vk_procs.h"

#include "core_types.h"

#include "vk_error.h"
// TODO: extend ?
struct vk_time_section
{
	const VkCommandBuffer& cmdBuff;
	const VkQueryPool& queryPool;
	const u32 queryIdx;

	inline vk_time_section( const VkCommandBuffer& _cmdBuff, const VkQueryPool& _queryPool, u32 _queryIdx ) 
		: cmdBuff{ _cmdBuff }, queryPool{ _queryPool }, queryIdx{ _queryIdx }
	{
		vkCmdWriteTimestamp2( cmdBuff, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, queryPool, queryIdx );
	}

	inline ~vk_time_section()
	{
		// VK_PIPELINE_STAGE_2_NONE
		vkCmdWriteTimestamp2( cmdBuff, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queryPool, queryIdx + 1 );
	}
};

// TODO: redesign
// NOTE: for timestamps we need 2 queries 
struct vk_gpu_timer
{
	vk_buffer resultBuff;
	VkQueryPool queryPool;
	u32         queryCount;
	float       timestampPeriod;
};

inline vk_gpu_timer VkMakeGpuTimer( VkDevice vkDevice, u32 timerRegionsCount, float tsPeriod, VkPhysicalDevice gpu, vk_mem_arena& arena )
{
	u32 queryCount = 2 * timerRegionsCount;
	vk_buffer resultBuff = VkCreateAllocBindBuffer( queryCount * sizeof( u64 ), VK_BUFFER_USAGE_TRANSFER_DST_BIT, gpu, arena );
	VkDbgNameObj( resultBuff.hndl, vkDevice, "Buff_Timestamp_Queries" );

	VkQueryPoolCreateInfo queryPoolInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryPoolInfo.queryCount = queryCount;
	queryPoolInfo.pipelineStatistics;

	VkQueryPool queryPool = {};
	VK_CHECK( vkCreateQueryPool( vkDevice, &queryPoolInfo, 0, &queryPool ) );
	VkDbgNameObj( queryPool, vkDevice, "VkQueryPool_GPU_timer" );

	return { resultBuff, queryPool, queryCount, tsPeriod };
}

inline float VkCmdReadGpuTimeInMs( VkCommandBuffer cmdBuff, const vk_gpu_timer& vkTimer )
{
	vkCmdCopyQueryPoolResults( 
		cmdBuff, vkTimer.queryPool, 0, vkTimer.queryCount, vkTimer.resultBuff.hndl, 0, sizeof( u64 ),
		VK_QUERY_RESULT_64_BIT );// | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT );

	auto readTimestampsBarrier = VkMakeBufferBarrier2(
		vkTimer.resultBuff.hndl,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_HOST_READ_BIT,
		VK_PIPELINE_STAGE_2_HOST_BIT );
	VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers = &readTimestampsBarrier;
	vkCmdPipelineBarrier2( cmdBuff, &dependency );

	const u64* pTimestamps = ( const u64* ) vkTimer.resultBuff.hostVisible;
	u64 timestampBeg = pTimestamps[ 0 ];
	u64 timestampEnd = pTimestamps[ 1 ];

	constexpr float nsToMs = 1e-6;
	return ( timestampEnd - timestampBeg ) / vkTimer.timestampPeriod * nsToMs;
}

inline void VkResetGpuTimer( VkCommandBuffer cmdBuff, const vk_gpu_timer& timer )
{
	vkCmdResetQueryPool( cmdBuff, timer.queryPool, 0, timer.queryCount );
}
#endif // !__VK_TIMER_H__
