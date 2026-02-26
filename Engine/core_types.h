#ifndef __CORE_TYPES_H__
#define __CORE_TYPES_H__

#include <stdint.h>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

//template<typename T>
struct range
{
	u64 offset;
	u64 size;

	//inline u64 OffsetInBytes() const
	//{
	//	return offset * sizeof( T );
	//}
	//inline u64 SizeInBytes() const
	//{
	//	return size * sizeof( T );
	//}
};

constexpr u64 INVALID_IDX = -1;

#endif // !__CORE_TYPES_H__
