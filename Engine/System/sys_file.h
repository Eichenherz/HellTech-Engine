#ifndef __SYS_FILE_H__
#define __SYS_FILE_H__

#include "core_types.h"
#include <span>
#include <string_view>
#include <memory>

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

	inline iterator			begin() { return data(); }
	inline iterator			end() { return data() + size(); }

	inline const_iterator	cbegin() const { return data(); }
	inline const_iterator	cend()   const { return data() + size(); }

	virtual u64				size() const = 0;
	virtual u8*				data() = 0;
	virtual const u8*		data() const = 0;

	virtual u64				Timestamp() = 0;
};

using PfnDestroyMmapFile = void(*)( mmap_file* );

using unique_mmap_file = std::unique_ptr<mmap_file, PfnDestroyMmapFile>;

unique_mmap_file SysCreateFile( 
	std::string_view path, 
	file_permissions_flags permissionFlags,
	file_create_flags createFlags,
	file_access_flags accessFalgs
);

#endif // !__SYS_FILE_H__