#ifndef __HELLTECH__
#define __HELLTECH__

#include "helltech_config.hpp"

#include <Vulkan/vk_backend.hpp>
#include <System/sys_platform.hpp>
#include <System/sys_input.hpp>

struct helltech
{
	vk_backend* pVkBackend;
	input_manager* pInputManager;
	sys_window* pSysWindow;
	

	helltech( sys_window* pSysWindow );
	~helltech() {}

	void CoreLoop();
};

#endif // !__HELLTECH__
