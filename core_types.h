#pragma once

#ifndef __CORE_TYPES__
#define __CORE_TYPES__

using u8 = unsigned __int8;
using u16 = unsigned __int16;
using u32 = unsigned __int32;
using u64 = unsigned __int64;

using i8 = __int8;
using i16 = __int16;
using i32 = __int32;
using i64 = __int64;

struct range
{
	u64 offset : 32;
	u64 size : 32;
};

constexpr u32 INVALID_IDX = -1;

#include <stddef.h>

#endif // !__CORE_TYPES__


