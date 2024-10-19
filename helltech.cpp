#include "helltech.hpp"

#include <string>
#include <imgui/imgui.h>

inline void ImGuiInit( ImVec2 displaySize )
{
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = displaySize;
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
}

struct imgui_window
{
	std::string_view title;
	std::string_view text;
	ImVec2 pos = {};
	ImGuiWindowFlags flags;
};

constexpr ImGuiWindowFlags IMGUI_SIMPLE_TEXT_WND_FLAGS = 
ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

inline void ImGuiPrepareWindow( const imgui_window& imgWnd )
{
	ImGui::SetNextWindowPos( imgWnd.pos );
	ImGui::SetNextWindowSize( { std::size( imgWnd.text ) * ImGui::GetFontSize(),50 } );
	ImGui::Begin( std::data( imgWnd.title ), 0, imgWnd.flags );
	ImGui::Text( std::data( imgWnd.text ) );
	ImGui::End();
}

inline void ImguiSubmitWidgets( const std::initializer_list<imgui_window> imguiWnds )
{
	ImGui::NewFrame();

	for( const auto& imguiWnd : imguiWnds )
	{
		ImGuiPrepareWindow( imguiWnd );
	}

	ImGui::Render();
	ImGui::EndFrame();
}

helltech::helltech( sys_window* pSysWindow )
{
	this->pSysWindow = pSysWindow;
	this->pVkBackend = new vk_backend( this->pSysWindow );

	ImGuiInit( { SCREEN_WIDTH, SCREEN_HEIGHT } );
}

void helltech::CoreLoop()
{
	// NOTE: time is a double of seconds
	// NOTE: t0 = double( UINT64( 1ULL << 32 ) ) -> precision mostly const for the next ~136 years;
	// NOTE: double gives time precision of 1 uS
	const double invFreq = 1.0 / ( double ) SysGetCpuFreq();
	//constexpr double	dt = 0.01;
	//double				t = double( UINT64( 1ULL << 32 ) );
	//double				accumulator = 0;
	u64					currentTicks = SysTicks();

	for( ;; )
	{
		const u64 newTicks = SysTicks();
		const double elapsedSecs = double( newTicks - currentTicks ) * invFreq;
		currentTicks = newTicks;
		//accumulator += elapsedSecs;

		// Clear input
		bool isRunning = SysPumpPlatfromMessages();
		if( !isRunning )
		{
			break;
		}

		// TODO: smooth camera some more ?
		auto [camMove, dPos] = GetCameraInput( *pInputManager );
		mainCam.Move( camMove, dPos, elapsedSecs );


		// TRANSF CAM VIEW
		XMMATRIX view = mainCam.ViewMatrix();


		XMStoreFloat4x4A( &frameData.proj, proj );
		XMStoreFloat4x4A( &frameData.activeView, view );
		if( !kbd.f )
		{
			XMStoreFloat4x4A( &frameData.mainView, view );
		}
		frameData.worldPos = camWorldPos;

		XMVECTOR viewDet = XMMatrixDeterminant( view );
		XMMATRIX invView = XMMatrixInverse( &viewDet, view );
		XMStoreFloat3( &frameData.camViewDir, XMVectorNegate( invView.r[ 2 ] ) );


		XMStoreFloat4x4A( &frameData.frustTransf, frustMat );

		XMStoreFloat4x4A( &frameData.activeProjView, XMMatrixMultiply( XMLoadFloat4x4A( &frameData.activeView ), proj ) );
		XMStoreFloat4x4A( &frameData.mainProjView, XMMatrixMultiply( XMLoadFloat4x4A( &frameData.mainView ), proj ) );

		frameData.elapsedSeconds = elapsedSecs;

		// NOT frame data
		frameData.freezeMainView = kbd.f;
		frameData.dbgDraw = kbd.o;

		// TODO: make own small efficient string
		std::string wndMsg( std::to_string( gpuData.timeMs ) );
		imgui_window gpuTimeDisplay = { .title = "GPU ms:", .text = wndMsg, .pos = {}, .flags = IMGUI_SIMPLE_TEXT_WND_FLAGS };
		ImguiSubmitWidgets( { gpuTimeDisplay } );

		// Simulation

		pVkBackend->HostFrames( frameData, gpuData );
	}
}
