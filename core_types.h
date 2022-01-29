#pragma once
#include <stdint.h>

using u8 = unsigned __int8;
using u16 = unsigned __int16;
using u32 = unsigned __int32;
using u64 = unsigned __int64;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = __int8;
using i16 = __int16;
using i32 = __int32;
using i64 = __int64;
using i32 = int32_t;
using i64 = int64_t;

struct range
{
	u64 offset : 32;
	u64 size : 32;
};

constexpr u64 INVALID_IDX = -1;