#ifndef __SYS_INPUT__
#define __SYS_INPUT__

#include "core_types.h"
#include <queue>
#include <bitset>
#include <Math/directx_math.hpp>

using virtual_key = u8;

// TODO: std::array/C array based queue
// TODO: more handlers ? interface ?
struct keyboard_handler
{
	struct message
	{
		virtual_key vk;
		u8 pressed : 1;
	};
	// meta data ?
	std::queue<message> eventQueue;
	std::bitset<256u> keyStates;

	void ClearState()
	{
		keyStates.reset();
	}
};

struct mouse_handler
{
	// TODO: mouse has different "buttons"
	struct message 
	{
		float dx;
		float dy;
	};
	// meta data ?
	std::queue<message> eventQueue;
	DirectX::XMFLOAT2 pos;
	float sensitivity = 0.1f;

	DirectX::XMFLOAT2 GetPos() const
	{
		return { pos.x * sensitivity, pos.y * sensitivity };
	}
};

template<typename T>
void TrimQueue( std::queue<T>& q, size_t maxSize )
{
	while( std::size( q ) > maxSize )
	{
		q.pop();
	}
}

template<typename T>
auto QueuePop( std::queue<T>& q )
{
	auto var = std::move( q.front() );
	q.pop();
	return var;
}

struct input_manager 
{
	static constexpr size_t MAX_QUEUE_MSG = 256;

	keyboard_handler kbd;
	mouse_handler mouse;
	bool hasFocus;

	void ProcessEvent( const keyboard_handler::message& message )
	{
		kbd.keyStates[ message.vk ] = message.pressed;
		kbd.eventQueue.push( message );
		TrimQueue( kbd.eventQueue, MAX_QUEUE_MSG );
	}
	void ProcessEvent( const mouse_handler::message& message )
	{
		mouse.pos = { message.dx, message.dy };
		mouse.eventQueue.push( message );
		TrimQueue( mouse.eventQueue, MAX_QUEUE_MSG );
	}
};

inline auto GetCameraInput( const input_manager& input )
{
	using namespace DirectX;

	XMVECTOR camMove = XMVectorSet( 0, 0, 0, 0 );
	const auto& kbd = input.kbd;
	if( kbd.keyStates[ 'W' ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, 1, 0 ) );
	if( kbd.keyStates[ 'A' ] ) camMove = XMVectorAdd( camMove, XMVectorSet( -1, 0, 0, 0 ) );
	if( kbd.keyStates[ 'S' ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, -1, 0 ) );
	if( kbd.keyStates[ 'D' ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 1, 0, 0, 0 ) );
	if( kbd.keyStates[ VK_SPACE ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 1, 0, 0 ) );
	if( kbd.keyStates[ 'C' ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, -1, 0, 0 ) );

	struct __retval
	{
		XMVECTOR move;
		XMFLOAT2 dPos;
	};

	return __retval{ camMove, input.mouse.GetPos() };
}

#endif // !__SYS_INPUT__
