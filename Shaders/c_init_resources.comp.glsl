#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_shuffle: require

#extension GL_GOOGLE_include_directive: require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"


#extension GL_EXT_debug_printf : enable


layout( push_constant, scalar ) uniform block{
	uint64_t	instDescAddr;
	uint64_t	meshDescAddr;
	uint64_t    visInstAddr;
	uint64_t    atomicWorkgrCounterAddr;
	uint64_t    drawCounterAddr;
	uint64_t    dispatchCmdAddr;
	uint	    hizBuffIdx;
	uint	    hizSamplerIdx;
	uint		instCount;
};

void main()
{
	
}