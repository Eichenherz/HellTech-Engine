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

struct file
{
	virtual size_t Size() = 0;
	virtual u8* Data() = 0;
	virtual u64 Timestamp() = 0;
	virtual std::span<u8> Span() = 0;
	virtual ~file() {};
};

// TODO: add more stuff as needed
std::unique_ptr<file> SysCreateFile( std::string_view path, file_permissions_flags filePermissions );

#endif // !__SYS_FILE_H__