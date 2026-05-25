#pragma once

#ifndef __HELLTECH_IM_GUI_H__
#define __HELLTECH_IM_GUI_H__

#include <imgui.h>
#include <ImGuiFileDialog.h>

#include <ht_fixed_string.h>

#include <System/Win32/win32_kbd_scancodes.h>

#include <vector>

struct im_gui_ctx
{
	ImGuiContext*	ctx = nullptr;
	ImGuiIO*		io	= nullptr;

	im_gui_ctx() = default;

	im_gui_ctx( u32 width, u32 height )
	{
		ctx = ImGui::CreateContext();
		ImGui::SetCurrentContext( ctx );
		ImGui::StyleColorsDark();
		io = &ImGui::GetIO();
		io->DisplaySize = { ( float ) width, ( float ) height };
		io->Fonts->AddFontDefault();
		io->Fonts->Build();
	}

	inline void UpdateTimeAndInputState( float elapsedSecs, const ht_input_state& inputState )
	{
		io->DeltaTime = elapsedSecs;
		io->MousePos = { inputState.mousePos.x, inputState.mousePos.y };
		io->MouseDown[ 0 ] = inputState.IsButtonDown( HT_MB_LEFT );
		io->MouseDown[ 1 ] = inputState.IsButtonDown( HT_MB_RIGHT );
		io->MouseDown[ 2 ] = inputState.IsButtonDown( HT_MB_MIDDLE );
		io->MouseDown[ 3 ] = inputState.IsButtonDown( HT_MB_4 );
		io->MouseDown[ 4 ] = inputState.IsButtonDown( HT_MB_5 );
	}
};

using imgui_widget_name = fixed_string<32>;

using imgui_window_name = fixed_string<32>;

enum class imgui_widget_type : u32
{
	BUTTON,
	CHECKBOX,
	RADIO,
	TEXT,           // ImGui::Text (read-only label)
	INPUT_TEXT,     // ImGui::InputText
	INPUT_INT,
	INPUT_FLOAT,
	DRAG_INT,
	DRAG_FLOAT,
	DRAG_FLOAT2,
	DRAG_FLOAT3,
	DRAG_FLOAT4,
	SLIDER_INT,
	SLIDER_FLOAT,
	COLOR3,         // ColorEdit3
	COLOR4,         // ColorEdit4
	COMBO,          // dropdown
	LISTBOX,
	TREE_NODE,
	COLLAPSING_HEADER,
	SEPARATOR,
	IMAGE,          // ImGui::Image
	PROGRESS_BAR,
	COUNT
};

using PFN_ImGuiWidgetAction = void( * )( const void* );

struct imgui_widget
{
	imgui_widget_name		name;
	const void*				pData;
	PFN_ImGuiWidgetAction	Action;
	imgui_widget_type		type;
};

struct imgui_window
{
	std::vector<imgui_widget>	widgets;
	imgui_window_name			name;
	ImGuiWindowFlags			flags;
};

inline void ImGuiHandleWidget( const imgui_widget& widget )
{
	using enum imgui_widget_type;
	switch( widget.type )
	{
		case BUTTON:
		{
			if( ImGui::Button( std::data( widget.name ) ) )
			{
				widget.Action( widget.pData );
			}

			break;
		}
		case TEXT:
		{
			if( std::size( widget.name ) )
			{
				ImGui::Text( "%s\x20", std::data( widget.name ) );
				ImGui::SameLine();
			}

			if( widget.pData )
			{
				widget.Action( widget.pData );
			}

			break;
		}
		case CHECKBOX:
		{
			ImGui::Checkbox( std::data( widget.name ), ( bool* ) widget.pData );
			break;
		}
		default: break;
	}
}

inline void ImGuiPrintFloatAction( const void* pData )
{
	HT_ASSERT( pData );
	ImGui::Text( "%.2f", *( const float* ) pData );
}

inline void ImGuiRenderUI( const std::vector<imgui_window>& imguiWnds )
{
	static bool initialPosSet = false;

	ImGui::NewFrame();

	for( const imgui_window& imguiWnd : imguiWnds )
	{
		if( !initialPosSet )
		{
			ImGui::SetNextWindowPos( { ( float ) std::size( imguiWnd.name ) * ImGui::GetFontSize(), ImGui::GetFontSize() },
				ImGuiCond_FirstUseEver );
			ImGui::SetNextWindowSize( {}, ImGuiCond_FirstUseEver );
			initialPosSet = true;
		}

		ImGui::Begin( std::data( imguiWnd.name ), 0, imguiWnd.flags );
		for( const imgui_widget& widget : imguiWnd.widgets )
		{
			ImGuiHandleWidget( widget );
		}
		ImGui::End();
	}

	ImGui::Render();

	ImGui::EndFrame();
}

#endif //!__HELLTECH_IM_GUI_H__