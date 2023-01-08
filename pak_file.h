#pragma once

#include "core_types.h"
#include <vector>

constexpr size_t MAX_FOLDER_PATH = 100;
constexpr size_t MAX_FILE_PATH = 255;
constexpr size_t MAX_PAK_NAME = 50;

struct pak_file_header
{
	char folderPath[ MAX_FOLDER_PATH ];
	char pakName[ MAX_PAK_NAME ];
	char magik[ 4 ] = { "PAK" };
	u32 contentVersion = 0;
	u8 version = 0;
	u8 NumEntries = 0;
};

struct pak_file_entry
{
	char filePath[ MAX_FILE_PATH ];
	u32 uncompressedSize;
	u32 compressedSize;
	u32 offset;
	bool compressed;
};

