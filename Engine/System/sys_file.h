#ifndef __SYS_FILE_H__
#define __SYS_FILE_H__

#include "ht_core_types.h"
#include <span>

// TODO: add more stuff as needed
enum file_permissions_bits : u64
{
	READ = 1,
	WRITE = 1 << 1
};

using file_permissions_flags = u64;

enum class file_create_flags : u64
{
	CREATE,
	OPEN_IF_EXISTS
};

enum class file_access_flags : u64
{
	SEQUENTIAL,
	RANDOM
};

struct mmap_file
{
	using iterator = u8*;
	using const_iterator = const u8*;

	u64				hFile			= ~u64{};
	u64				hFileMapping	= ~u64{};
	std::span<u8>	dataView		= {};


	inline iterator			begin()			{ return data(); }
	inline iterator			end()			{ return data() + size(); }

	inline const_iterator	cbegin() const	{ return data(); }
	inline const_iterator	cend()   const	{ return data() + size(); }

	inline u64				size() const	{ return std::size( dataView ); }
	inline u8*				data()			{ return std::data( dataView ); }
	inline const u8*		data() const	{ return std::data( dataView ); }

	u64						Timestamp();
};

using PfnDestroyMmapFile = void(*)( mmap_file* );

mmap_file SysCreateMmapFile(
	const char*				path,
	file_permissions_flags	permissionFlags,
	file_create_flags		createFlags,
	file_access_flags		accessFlags
);

void SysDestroyMmapFile( mmap_file* mmapFile );

#endif // !__SYS_FILE_H__