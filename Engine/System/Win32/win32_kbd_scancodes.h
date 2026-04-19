#pragma once

#ifndef __HELLTECH_WIN32_KBD_SCANCODES_H__
#define __HELLTECH_WIN32_KBD_SCANCODES_H__

#include <ht_core_types.h>

// Keyboard scancodes (PS/2 Set 1, Windows RAWKEYBOARD.MakeCode)
// E0-prefixed keys have bit 0x100 set (matches HT_KeyIndex convention)

// --- Main block (0x00-0x7F) ---
constexpr u16 HT_SC_ESCAPE        = 0x01;
constexpr u16 HT_SC_1             = 0x02;
constexpr u16 HT_SC_2             = 0x03;
constexpr u16 HT_SC_3             = 0x04;
constexpr u16 HT_SC_4             = 0x05;
constexpr u16 HT_SC_5             = 0x06;
constexpr u16 HT_SC_6             = 0x07;
constexpr u16 HT_SC_7             = 0x08;
constexpr u16 HT_SC_8             = 0x09;
constexpr u16 HT_SC_9             = 0x0A;
constexpr u16 HT_SC_0             = 0x0B;
constexpr u16 HT_SC_MINUS         = 0x0C;
constexpr u16 HT_SC_EQUALS        = 0x0D;
constexpr u16 HT_SC_BACKSPACE     = 0x0E;
constexpr u16 HT_SC_TAB           = 0x0F;

constexpr u16 HT_SC_Q             = 0x10;
constexpr u16 HT_SC_W             = 0x11;
constexpr u16 HT_SC_E             = 0x12;
constexpr u16 HT_SC_R             = 0x13;
constexpr u16 HT_SC_T             = 0x14;
constexpr u16 HT_SC_Y             = 0x15;
constexpr u16 HT_SC_U             = 0x16;
constexpr u16 HT_SC_I             = 0x17;
constexpr u16 HT_SC_O             = 0x18;
constexpr u16 HT_SC_P             = 0x19;
constexpr u16 HT_SC_LEFTBRACKET   = 0x1A;
constexpr u16 HT_SC_RIGHTBRACKET  = 0x1B;
constexpr u16 HT_SC_RETURN        = 0x1C;
constexpr u16 HT_SC_LCTRL         = 0x1D;

constexpr u16 HT_SC_A             = 0x1E;
constexpr u16 HT_SC_S             = 0x1F;
constexpr u16 HT_SC_D             = 0x20;
constexpr u16 HT_SC_F             = 0x21;
constexpr u16 HT_SC_G             = 0x22;
constexpr u16 HT_SC_H             = 0x23;
constexpr u16 HT_SC_J             = 0x24;
constexpr u16 HT_SC_K             = 0x25;
constexpr u16 HT_SC_L             = 0x26;
constexpr u16 HT_SC_SEMICOLON     = 0x27;
constexpr u16 HT_SC_APOSTROPHE    = 0x28;
constexpr u16 HT_SC_GRAVE         = 0x29;
constexpr u16 HT_SC_LSHIFT        = 0x2A;
constexpr u16 HT_SC_BACKSLASH     = 0x2B;

constexpr u16 HT_SC_Z             = 0x2C;
constexpr u16 HT_SC_X             = 0x2D;
constexpr u16 HT_SC_C             = 0x2E;
constexpr u16 HT_SC_V             = 0x2F;
constexpr u16 HT_SC_B             = 0x30;
constexpr u16 HT_SC_N             = 0x31;
constexpr u16 HT_SC_M             = 0x32;
constexpr u16 HT_SC_COMMA         = 0x33;
constexpr u16 HT_SC_PERIOD        = 0x34;
constexpr u16 HT_SC_SLASH         = 0x35;
constexpr u16 HT_SC_RSHIFT        = 0x36;
constexpr u16 HT_SC_KP_MULTIPLY   = 0x37;
constexpr u16 HT_SC_LALT          = 0x38;
constexpr u16 HT_SC_SPACE         = 0x39;
constexpr u16 HT_SC_CAPSLOCK      = 0x3A;

constexpr u16 HT_SC_F1            = 0x3B;
constexpr u16 HT_SC_F2            = 0x3C;
constexpr u16 HT_SC_F3            = 0x3D;
constexpr u16 HT_SC_F4            = 0x3E;
constexpr u16 HT_SC_F5            = 0x3F;
constexpr u16 HT_SC_F6            = 0x40;
constexpr u16 HT_SC_F7            = 0x41;
constexpr u16 HT_SC_F8            = 0x42;
constexpr u16 HT_SC_F9            = 0x43;
constexpr u16 HT_SC_F10           = 0x44;
constexpr u16 HT_SC_NUMLOCK       = 0x45;
constexpr u16 HT_SC_SCROLLLOCK    = 0x46;

constexpr u16 HT_SC_KP_7          = 0x47;
constexpr u16 HT_SC_KP_8          = 0x48;
constexpr u16 HT_SC_KP_9          = 0x49;
constexpr u16 HT_SC_KP_MINUS      = 0x4A;
constexpr u16 HT_SC_KP_4          = 0x4B;
constexpr u16 HT_SC_KP_5          = 0x4C;
constexpr u16 HT_SC_KP_6          = 0x4D;
constexpr u16 HT_SC_KP_PLUS       = 0x4E;
constexpr u16 HT_SC_KP_1          = 0x4F;
constexpr u16 HT_SC_KP_2          = 0x50;
constexpr u16 HT_SC_KP_3          = 0x51;
constexpr u16 HT_SC_KP_0          = 0x52;
constexpr u16 HT_SC_KP_PERIOD     = 0x53;

constexpr u16 HT_SC_NONUSBACKSLASH = 0x56;
constexpr u16 HT_SC_F11           = 0x57;
constexpr u16 HT_SC_F12           = 0x58;
constexpr u16 HT_SC_KP_EQUALS     = 0x59;

constexpr u16 HT_SC_F13           = 0x64;
constexpr u16 HT_SC_F14           = 0x65;
constexpr u16 HT_SC_F15           = 0x66;
constexpr u16 HT_SC_F16           = 0x67;
constexpr u16 HT_SC_F17           = 0x68;
constexpr u16 HT_SC_F18           = 0x69;
constexpr u16 HT_SC_F19           = 0x6A;
constexpr u16 HT_SC_F20           = 0x6B;
constexpr u16 HT_SC_F21           = 0x6C;
constexpr u16 HT_SC_F22           = 0x6D;
constexpr u16 HT_SC_F23           = 0x6E;
constexpr u16 HT_SC_F24           = 0x76;

// --- E0-prefixed keys (OR'd with 0x100 to match HT_KeyIndex) ---
constexpr u16 HT_SC_MEDIA_PREV    = 0x110; // 0xE0 0x10
constexpr u16 HT_SC_MEDIA_NEXT    = 0x119; // 0xE0 0x19
constexpr u16 HT_SC_KP_ENTER      = 0x11C; // 0xE0 0x1C
constexpr u16 HT_SC_RCTRL         = 0x11D; // 0xE0 0x1D
constexpr u16 HT_SC_MUTE          = 0x120;
constexpr u16 HT_SC_MEDIA_PLAY    = 0x122;
constexpr u16 HT_SC_MEDIA_STOP    = 0x124;
constexpr u16 HT_SC_VOLUME_DOWN   = 0x12E;
constexpr u16 HT_SC_VOLUME_UP     = 0x130;
constexpr u16 HT_SC_KP_DIVIDE     = 0x135;
constexpr u16 HT_SC_PRINTSCREEN   = 0x137;
constexpr u16 HT_SC_RALT          = 0x138;
constexpr u16 HT_SC_PAUSE         = 0x146; // E1 variant, see note below
constexpr u16 HT_SC_HOME          = 0x147;
constexpr u16 HT_SC_UP            = 0x148;
constexpr u16 HT_SC_PAGEUP        = 0x149;
constexpr u16 HT_SC_LEFT          = 0x14B;
constexpr u16 HT_SC_RIGHT         = 0x14D;
constexpr u16 HT_SC_END           = 0x14F;
constexpr u16 HT_SC_DOWN          = 0x150;
constexpr u16 HT_SC_PAGEDOWN      = 0x151;
constexpr u16 HT_SC_INSERT        = 0x152;
constexpr u16 HT_SC_DELETE        = 0x153;
constexpr u16 HT_SC_LGUI          = 0x15B; // left Windows key
constexpr u16 HT_SC_RGUI          = 0x15C; // right Windows key
constexpr u16 HT_SC_APPLICATION   = 0x15D; // context menu key

#endif //!__HELLTECH_WIN32_KBD_SCANCODES_H__