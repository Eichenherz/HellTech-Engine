#pragma once

//////////////////////////////////////
// MACROS
//////////////////////////////////////
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STR STRINGIZE(__LINE__)

#define RUNTIME_ERR_LINE_FILE_STR ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__


