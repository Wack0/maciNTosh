/*
 *
 * Open Hack'Ware BIOS ADB keyboard support, ported to OpenBIOS
 *
 *  Copyright (c) 2005 Jocelyn Mayer
 *  Copyright (c) 2005 Stefan Reinauer
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License V2
 *   as published by the Free Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA, 02110-1301 USA
 */

#include <stdio.h>

#include "arc.h"
#include "adb_bus.h"
#include "adb_kbd.h"
#include "usb.h"

enum {
	ADB_MAX_SEQUENCE_LEN = 16
};

enum {
	KEY_MODIFIER_LCTRL = BIT(0),
	KEY_MODIFIER_LSHIFT = BIT(1),
	KEY_MODIFIER_LALT = BIT(2),
	KEY_MODIFIER_LWIN = BIT(3),
	KEY_MODIFIER_RCTRL = BIT(4),
	KEY_MODIFIER_RSHIFT = BIT(5),
	KEY_MODIFIER_RALT = BIT(6),
	KEY_MODIFIER_RWIN = BIT(7),

	KEY_MODIFIER_CTRL = KEY_MODIFIER_LCTRL | KEY_MODIFIER_RCTRL,
	KEY_MODIFIER_SHIFT = KEY_MODIFIER_LSHIFT | KEY_MODIFIER_RSHIFT,
	KEY_MODIFIER_ALT = KEY_MODIFIER_LALT | KEY_MODIFIER_RALT,
	KEY_MODIFIER_WIN = KEY_MODIFIER_LWIN | KEY_MODIFIER_RWIN
};

enum {
	KEY_ERROR_OVF = 1
};

// We convert from ADB to USB then to char.
enum {
	ADB_KEY_LCTRL = 0x36,
	ADB_KEY_LSHIFT = 0x38,
	ADB_KEY_LALT = 0x3A,
	ADB_KEY_LWIN = 0x37,
	ADB_KEY_RCTRL = 0x7D,
	ADB_KEY_RSHIFT = 0x7B,
	ADB_KEY_RALT = 0x7C,
	ADB_KEY_RWIN = 0x7E,
	ADB_KEY_CAPSLOCK = 0x39,
};

enum {
	ADB_LAYOUT_ANSI,
	ADB_LAYOUT_ISO,
	ADB_LAYOUT_JIS
};

// This table comes from darwin code.
// Modified by using other ADB scancode tables as a base (the original used virtual scancodes in places, not physical)
// Added the key it corresponds to based on UK keyboard layout where appropriate
static const UCHAR sc_AdbToUsbTable[] =
{
	0x04,	// 0  a
	0x16,	// 1  s
	0x07,	// 2  d
	0x09,	// 3  f
	0x0b,	// 4  h
	0x0a,	// 5  g
	0x1d,	// 6  z
	0x1b,	// 7  x
	0x06,	// 8  c
	0x19,	// 9  v
	0x64,	// a  [\|]
	0x05,	// b  b
	0x14,	// c  q
	0x1a,	// d  w
	0x08,	// e  e
	0x15,	// f  r
	0x1c,	// 10 y
	0x17,	// 11 t
	0x1e,	// 12 1!
	0x1f,	// 13 2"
	0x20,	// 14 3£
	0x21,	// 15 4$
	0x23,	// 16 6^
	0x22,	// 17 5%
	0x2e,	// 18 =+
	0x26,	// 19 9(
	0x24,	// 1a 7&
	0x2d,	// 1b -_
	0x25,	// 1c 8*
	0x27,	// 1d 0)
	0x30,	// 1e ]}
	0x12,	// 1f o
	0x18,	// 20 u
	0x2f,	// 21 [{
	0x0c,	// 22 i
	0x13,	// 23 p
	0x28,	// 24 (ENTER)
	0x0f,	// 25 l
	0x0d,	// 26 j
	0x34,	// 27 '@
	0x0e,	// 28 k
	0x33,	// 29 ;:
	0x31,	// 2a \|
	0x36,	// 2b ,<
	0x38,	// 2c /?
	0x11,	// 2d n
	0x10,	// 2e m
	0x37,	// 2f .>
	0x2b,	// 30 [TAB]
	0x2c,	// 31 [space]
	0x35,	// 32 `¬
	0x2a,	// 33 [backspace]
	0x58,	// 34 [numpad ENTER]
	0x29,	// 35 [ESC]
	0xe0,	// 36 [left ctrl]
	0xe3,	// 37 [left win]
	0xe1,	// 38 [left shift]
	0x39,	// 39 [caps lock]
	0xe2,	// 3a [left alt]
	0x50,	// 3b [left]
	0x4f,	// 3c [right]
	0x51,	// 3d [down]
	0x52,	// 3e [up]
	0x00,	// 3f [none]
	0x6c,	// 40 [F17]
	0x63,	// 41 [numpad .]
	0x00,	// 42 [none]
	0x55,	// 43 [numpad *]
	0x6d,	// 44 [F18]
	0x57,	// 45 [numpad +]
	0x00,	// 46 [none]
	0x53,	// 47 [num lock]
	0xed,	// 48 [vol+]
	0xee,	// 49 [vol-]
	0xef,	// 4a [mute]
	0x54,	// 4b [numpad /]
	0x58,	// 4c [numpad ENTER]
	0x00,	// 4d [none]
	0x56,	// 4e [numpad -]
	0x6d,	// 4f [F18]
	0x6e,	// 50 [F19]
	0x67,	// 51 [numpad =]
	0x62,	// 52 [numpad 0]
	0x59,	// 53 [numpad 1]
	0x5a,	// 54 [numpad 2]
	0x5b,	// 55 [numpad 3]
	0x5c,	// 56 [numpad 4]
	0x5d,	// 57 [numpad 5]
	0x5e,	// 58 [numpad 6]
	0x5f,	// 59 [numpad 7]
	0x6f,	// 5a [F20]
	0x60,	// 5b [numpad 8]
	0x61,	// 5c [numpad 9]
	0x89,	// 5d [JIS: yen/intl3]
	0x87,	// 5e [JIS: ro/intl1]
	0x85,	// 5f [JIS: numpad ,]
	0x3e,	// 60 [F5]
	0x3f,	// 61 [F6]
	0x40,	// 62 [F7]
	0x3c,	// 63 [F3]
	0x41,	// 64 [F8]
	0x42,	// 65 [F9]
	0x91,	// 66 [hanja/lang2/eisu]
	0x44,	// 67 [F11]
	0x90,	// 68 [hangeul/lang1/kana]
	0x68,	// 69 [F13/prtsc]
	0x6b,	// 6a [F16]
	0x69,	// 6b [F14/scroll lock]
	0x0,	// 6c [none]
	0x43,	// 6d [F10]
	0x65,	// 6e [compose]
	0x45,	// 6f [F12]
	0x32,	// 70 #~
	0x48,	// 71 [pause/F15]
	0x49,	// 72 [insert] (help?)
	0x4a,	// 73 [home]
	0x4b,	// 74 [pageup]
	0x4c,	// 75 [delete]
	0x3d,	// 76 [F4]
	0x4d,	// 77 [end]
	0x3b,	// 78 [F2]
	0x4e,	// 79 [pagedown]
	0x3a,	// 7a [F1]
	0xe5,	// 7b [right shift]
	0xe6,	// 7c [right alt]
	0xe4,	// 7d [right ctrl]
	0xe5,	// 7e [right win]
	0x66,	// 7f [power] -- actually 7f7f and handled seperately
};

// USB keyboard constants etc, copypasta'd from Wii ARC firmware
enum {
	KEY_VALID_START = 2,
	KEY_LOOKUP_START = 4,
	KEY_LOOKUP_END = 0x38,

	KEY_CAPSLOCK = 0x39, // Keyboard Caps Lock

	KEY_F1 = 0x3a, // Keyboard F1
	KEY_F2 = 0x3b, // Keyboard F2
	KEY_F3 = 0x3c, // Keyboard F3
	KEY_F4 = 0x3d, // Keyboard F4
	KEY_F5 = 0x3e, // Keyboard F5
	KEY_F6 = 0x3f, // Keyboard F6
	KEY_F7 = 0x40, // Keyboard F7
	KEY_F8 = 0x41, // Keyboard F8
	KEY_F9 = 0x42, // Keyboard F9
	KEY_F10 = 0x43, // Keyboard F10
	KEY_F11 = 0x44, // Keyboard F11
	KEY_F12 = 0x45, // Keyboard F12

	KEY_SYSRQ = 0x46, // Keyboard Print Screen
	KEY_SCROLLLOCK = 0x47, // Keyboard Scroll Lock
	KEY_PAUSE = 0x48, // Keyboard Pause
	KEY_INSERT = 0x49, // Keyboard Insert
	KEY_HOME = 0x4a, // Keyboard Home
	KEY_PAGEUP = 0x4b, // Keyboard Page Up
	KEY_DELETE = 0x4c, // Keyboard Delete Forward
	KEY_END = 0x4d, // Keyboard End
	KEY_PAGEDOWN = 0x4e, // Keyboard Page Down
	KEY_RIGHT = 0x4f, // Keyboard Right Arrow
	KEY_LEFT = 0x50, // Keyboard Left Arrow
	KEY_DOWN = 0x51, // Keyboard Down Arrow
	KEY_UP = 0x52, // Keyboard Up Arrow

	KEY_NUMLOCK = 0x53, // Keyboard Num Lock and Clear
	KEY_KPSLASH = 0x54, // Keypad /
	KEY_KPASTERISK = 0x55, // Keypad *
	KEY_KPMINUS = 0x56, // Keypad -
	KEY_KPPLUS = 0x57, // Keypad +
	KEY_KPENTER = 0x58, // Keypad ENTER
	KEY_KP1 = 0x59, // Keypad 1 and End
	KEY_KP2 = 0x5a, // Keypad 2 and Down Arrow
	KEY_KP3 = 0x5b, // Keypad 3 and PageDn
	KEY_KP4 = 0x5c, // Keypad 4 and Left Arrow
	KEY_KP5 = 0x5d, // Keypad 5
	KEY_KP6 = 0x5e, // Keypad 6 and Right Arrow
	KEY_KP7 = 0x5f, // Keypad 7 and Home
	KEY_KP8 = 0x60, // Keypad 8 and Up Arrow
	KEY_KP9 = 0x61, // Keypad 9 and Page Up
	KEY_KP0 = 0x62, // Keypad 0 and Insert
	KEY_KPDOT = 0x63, // Keypad . and Delete

	KEY_102ND = 0x64, // Keyboard Non-US \ and |
	KEY_COMPOSE = 0x65, // Keyboard Application
	KEY_POWER = 0x66, // Keyboard Power
	KEY_KPEQUAL = 0x67, // Keypad =

	KEY_F13 = 0x68, // Keyboard F13
	KEY_F14 = 0x69, // Keyboard F14
	KEY_F15 = 0x6a, // Keyboard F15
	KEY_F16 = 0x6b, // Keyboard F16
	KEY_F17 = 0x6c, // Keyboard F17
	KEY_F18 = 0x6d, // Keyboard F18
	KEY_F19 = 0x6e, // Keyboard F19
	KEY_F20 = 0x6f, // Keyboard F20
	KEY_F21 = 0x70, // Keyboard F21
	KEY_F22 = 0x71, // Keyboard F22
	KEY_F23 = 0x72, // Keyboard F23
	KEY_F24 = 0x73, // Keyboard F24

	KEY_OPEN = 0x74, // Keyboard Execute
	KEY_HELP = 0x75, // Keyboard Help
	KEY_PROPS = 0x76, // Keyboard Menu
	KEY_FRONT = 0x77, // Keyboard Select
	KEY_STOP = 0x78, // Keyboard Stop
	KEY_AGAIN = 0x79, // Keyboard Again
	KEY_UNDO = 0x7a, // Keyboard Undo
	KEY_CUT = 0x7b, // Keyboard Cut
	KEY_COPY = 0x7c, // Keyboard Copy
	KEY_PASTE = 0x7d, // Keyboard Paste
	KEY_FIND = 0x7e, // Keyboard Find
	KEY_MUTE = 0x7f, // Keyboard Mute
	KEY_VOLUMEUP = 0x80, // Keyboard Volume Up
	KEY_VOLUMEDOWN = 0x81, // Keyboard Volume Down
	// = 0x82  Keyboard Locking Caps Lock
	// = 0x83  Keyboard Locking Num Lock
	// = 0x84  Keyboard Locking Scroll Lock
	KEY_KPCOMMA = 0x85, // Keypad Comma
	// = 0x86  Keypad Equal Sign
	KEY_RO = 0x87, // Keyboard International1
	KEY_KATAKANAHIRAGANA = 0x88, // Keyboard International2
	KEY_YEN = 0x89, // Keyboard International3
	KEY_HENKAN = 0x8a, // Keyboard International4
	KEY_MUHENKAN = 0x8b, // Keyboard International5
	KEY_KPJPCOMMA = 0x8c, // Keyboard International6
	// = 0x8d  Keyboard International7
	// = 0x8e  Keyboard International8
	// = 0x8f  Keyboard International9
	KEY_HANGEUL = 0x90, // Keyboard LANG1
	KEY_HANJA = 0x91, // Keyboard LANG2
	KEY_KATAKANA = 0x92, // Keyboard LANG3
	KEY_HIRAGANA = 0x93, // Keyboard LANG4
	KEY_ZENKAKUHANKAKU = 0x94, // Keyboard LANG5
	// = 0x95  Keyboard LANG6
	// = 0x96  Keyboard LANG7
	// = 0x97  Keyboard LANG8
	// = 0x98  Keyboard LANG9
	// = 0x99  Keyboard Alternate Erase
	// = 0x9a  Keyboard SysReq/Attention
	// = 0x9b  Keyboard Cancel
	// = 0x9c  Keyboard Clear
	// = 0x9d  Keyboard Prior
	// = 0x9e  Keyboard Return
	// = 0x9f  Keyboard Separator
	// = 0xa0  Keyboard Out
	// = 0xa1  Keyboard Oper
	// = 0xa2  Keyboard Clear/Again
	// = 0xa3  Keyboard CrSel/Props
	// = 0xa4  Keyboard ExSel

	// = 0xb0  Keypad 00
	// = 0xb1  Keypad 000
	// = 0xb2  Thousands Separator
	// = 0xb3  Decimal Separator
	// = 0xb4  Currency Unit
	// = 0xb5  Currency Sub-unit
	KEY_KPLEFTPAREN = 0xb6, // Keypad (
	KEY_KPRIGHTPAREN = 0xb7, // Keypad )
	// = 0xb8  Keypad {
	// = 0xb9  Keypad }
	// = 0xba  Keypad Tab
	// = 0xbb  Keypad Backspace
	// = 0xbc  Keypad A
	// = 0xbd  Keypad B
	// = 0xbe  Keypad C
	// = 0xbf  Keypad D
	// = 0xc0  Keypad E
	// = 0xc1  Keypad F
	// = 0xc2  Keypad XOR
	// = 0xc3  Keypad ^
	// = 0xc4  Keypad %
	// = 0xc5  Keypad <
	// = 0xc6  Keypad >
	// = 0xc7  Keypad &
	// = 0xc8  Keypad &&
	// = 0xc9  Keypad |
	// = 0xca  Keypad ||
	// = 0xcb  Keypad :
	// = 0xcc  Keypad #
	// = 0xcd  Keypad Space
	// = 0xce  Keypad @
	// = 0xcf  Keypad !
	// = 0xd0  Keypad Memory Store
	// = 0xd1  Keypad Memory Recall
	// = 0xd2  Keypad Memory Clear
	// = 0xd3  Keypad Memory Add
	// = 0xd4  Keypad Memory Subtract
	// = 0xd5  Keypad Memory Multiply
	// = 0xd6  Keypad Memory Divide
	// = 0xd7  Keypad +/-
	// = 0xd8  Keypad Clear
	// = 0xd9  Keypad Clear Entry
	// = 0xda  Keypad Binary
	// = 0xdb  Keypad Octal
	// = 0xdc  Keypad Decimal
	// = 0xdd  Keypad Hexadecimal

	KEY_LEFTCTRL = 0xe0, // Keyboard Left Control
	KEY_LEFTSHIFT = 0xe1, // Keyboard Left Shift
	KEY_LEFTALT = 0xe2, // Keyboard Left Alt
	KEY_LEFTMETA = 0xe3, // Keyboard Left GUI
	KEY_RIGHTCTRL = 0xe4, // Keyboard Right Control
	KEY_RIGHTSHIFT = 0xe5, // Keyboard Right Shift
	KEY_RIGHTALT = 0xe6, // Keyboard Right Alt
	KEY_RIGHTMETA = 0xe7, // Keyboard Right GUI
};

#define KBD_BUFFER_SIZE 32

typedef struct _KEYBOARD_BUFFER {
	volatile UCHAR Buffer[KBD_BUFFER_SIZE];
	volatile UCHAR ReadIndex;
	volatile UCHAR WriteIndex;
} KEYBOARD_BUFFER, * PKEYBOARD_BUFFER;


static BYTE s_LastKeycode, s_LastModifier;
static KEYBOARD_BUFFER s_Buffer = { {0}, 0, 0 };
static USB_KBD_REPORT s_Report = { 0 };
static bool s_CapsLock = false;

// Two lookup tables, encompassing key codes between KEY_A(4) and KEY_SLASH(0x38).
// en_GB keyboard layout, whatever...
//static const UCHAR s_LookupNormal[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\x09 -=[]\\#;'`,./";
//static const UCHAR s_LookupShift [] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!\"£$%^&*()\n\x1b\b\x09 _+{}|~:@~<>?";
static const UCHAR s_LookupNormal[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\x09 -=[]\\\\;#',./";
static const UCHAR s_LookupShift[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!\"£$%^&*()\n\x1b\b\x09 _+{}||:~@<>?";

#define INCREMENT_INDEX(var) (((var) + 1) % sizeof(s_Buffer.Buffer))
#define INCREMENT_INDEX_READ() INCREMENT_INDEX(s_Buffer.ReadIndex)
#define INCREMENT_INDEX_WRITE() INCREMENT_INDEX(s_Buffer.WriteIndex)

static bool adb_kbd_poll(void);

static void KBD_Poll(void) {
	while (adb_kbd_poll());
	usb_poll();
}

UCHAR IOSKBD_ReadChar() {
	while (s_Buffer.ReadIndex == s_Buffer.WriteIndex) {
		KBD_Poll();
	}
	s_Buffer.ReadIndex = INCREMENT_INDEX_READ();
	return s_Buffer.Buffer[s_Buffer.ReadIndex];
}

bool IOSKBD_CharAvailable() {
	KBD_Poll();
	return s_Buffer.ReadIndex != s_Buffer.WriteIndex;
}

static void KBDWriteChar(UCHAR Character) {
	UCHAR IncWrite = INCREMENT_INDEX_WRITE();
	if (IncWrite != s_Buffer.ReadIndex) {
		s_Buffer.WriteIndex = IncWrite;
		s_Buffer.Buffer[s_Buffer.WriteIndex] = Character;
	}
}

#define KBDWriteString(str) for (int i = 0; i < sizeof(str)-1; i++) KBDWriteChar((str)[i]);

static void KBDWriteKey(BYTE keycode, BYTE modifier) {
	if (keycode < KEY_VALID_START) return;

	if (keycode == KEY_CAPSLOCK) {
		s_CapsLock ^= 1;
		return;
	}
	if (keycode == KEY_SYSRQ) {
		KBDWriteChar('\x80');
		return;
	}
	if (keycode >= KEY_LOOKUP_START && keycode <= KEY_LOOKUP_END) {
		if (s_CapsLock && keycode >= KEY_LOOKUP_START && keycode <= (KEY_LOOKUP_START + 26)) {
			if (modifier & KEY_MODIFIER_SHIFT) modifier &= ~KEY_MODIFIER_SHIFT;
			else modifier |= KEY_MODIFIER_SHIFT;
		}
		if (modifier & KEY_MODIFIER_SHIFT) {
			KBDWriteChar(s_LookupShift[keycode - KEY_LOOKUP_START]);
		}
		else {
			KBDWriteChar(s_LookupNormal[keycode - KEY_LOOKUP_START]);
		}
		return;
	}

	UCHAR ControlChar = 0;

	if (keycode >= KEY_F1 && keycode <= KEY_F12) {
		// This table comes from the ARC specification (ARC/riscspec.pdf) page 105.
		static const char s_ControlChars[] = "PQwxtuqrpMAB";
		KBDWriteChar('\x9b');
		KBDWriteChar('O');
		KBDWriteChar(s_ControlChars[keycode - KEY_F1]);
		return;
	}

	switch (keycode) {
	case KEY_UP:
		ControlChar = 'A';
		break;

	case KEY_DOWN:
		ControlChar = 'B';
		break;

	case KEY_RIGHT:
		ControlChar = 'C';
		break;

	case KEY_LEFT:
		ControlChar = 'D';
		break;

	case KEY_HOME:
		ControlChar = 'H';
		break;

	case KEY_END:
		ControlChar = 'K';
		break;

	case KEY_PAGEUP:
		ControlChar = '?';
		break;

	case KEY_PAGEDOWN:
		ControlChar = '/';
		break;

	case KEY_INSERT:
		ControlChar = '@';
		break;

	case KEY_DELETE:
		ControlChar = 'P';
		break;
	}

	if (ControlChar == 0) return;
	KBDWriteChar('\x9b');
	KBDWriteChar(ControlChar);
}

void KBDOnEvent(PUSB_KBD_REPORT Report) {
	// Only support the first key pressed.
	// Do not check modifier, this prevents things like "press shift, press ;:, release shift first" to input ":;"
	//if (s_LastKeycode == s_Event.report.keycode[0] && s_LastModifier == s_Event.report.modifier) return;
	if (s_LastKeycode == Report->KeyCode[0]) return;
	s_LastKeycode = Report->KeyCode[0];
	s_LastModifier = Report->Modifiers;
	KBDWriteKey(s_LastKeycode, s_LastModifier);
}

static UCHAR AkpGetKeyboardLayout(UCHAR Handler) {
	switch (Handler) {
	default:
		return ADB_LAYOUT_ANSI;
	case 0x04:
	case 0x05:
	case 0x07:
	case 0x09:
	case 0x0D:
	case 0x11:
	case 0x14:
	case 0x19:
	case 0x1D:
	case 0xC1:
	case 0xC4:
	case 0xC7:
		return ADB_LAYOUT_ISO;
	case 0x12:
	case 0x15:
	case 0x16:
	case 0x17:
	case 0x1A:
	case 0x1E:
	case 0xC2:
	case 0xC5:
	case 0xC8:
	case 0xC9:
		return ADB_LAYOUT_JIS;
	}
}

static bool AkpKeyIsModifier(UCHAR AdbScanCode) {
	switch (AdbScanCode) {
	case ADB_KEY_LCTRL:
	case ADB_KEY_LSHIFT:
	case ADB_KEY_LALT:
	case ADB_KEY_LWIN:
	case ADB_KEY_RCTRL:
	case ADB_KEY_RSHIFT:
	case ADB_KEY_RALT:
	case ADB_KEY_RWIN:
		return true;
	default:
		return false;
	}
	return false;
}

static bool AkpUsbAddKey(PUSB_KBD_REPORT Report, UCHAR UsbScanCode) {
	UCHAR FreeSlot = sizeof(Report->KeyCode);
	for (UCHAR i = 0; i < sizeof(Report->KeyCode); i++) {
		if (Report->KeyCode[i] == UsbScanCode) return true;
		if (Report->KeyCode[i] == 0 && FreeSlot >= sizeof(Report->KeyCode))
			FreeSlot = i;
	}

	if (FreeSlot >= sizeof(Report->KeyCode)) return false;
	Report->KeyCode[FreeSlot] = UsbScanCode;
	return true;
}

static bool AkpUsbRemoveKey(PUSB_KBD_REPORT Report, UCHAR UsbScanCode) {
	bool Changed = false;
	for (UCHAR i = 0; i < sizeof(Report->KeyCode); i++) {
		if (Report->KeyCode[i] != UsbScanCode) continue;
		Report->KeyCode[i] = 0;
		Changed = true;
	}
	return Changed;
}

static bool AkpUpdateKey(PUSB_KBD_REPORT Report, UCHAR UsbScanCode, bool Released) {
	if (UsbScanCode == 0) return false;
	if (Released) return AkpUsbRemoveKey(Report, UsbScanCode);
	return AkpUsbAddKey(Report, UsbScanCode);
}

static bool AkpUsbUpdateModifier(PUSB_KBD_REPORT Report, UCHAR UsbModifierBit, bool Released) {
	// check if the bit is already correct
	bool IsSet = (Report->Modifiers & UsbModifierBit) != 0;
	if (IsSet == !Released) return false;

	// bit is incorrect, so flip it
	Report->Modifiers ^= UsbModifierBit;
	return true;
}

static bool AkpUpdateModifiers(PUSB_KBD_REPORT Report, UCHAR AdbScanCode, bool Released) {
	switch (AdbScanCode) {
	case ADB_KEY_LCTRL:
		return AkpUsbUpdateModifier(Report, KEY_MODIFIER_LCTRL, Released);
	case ADB_KEY_LSHIFT:
		return AkpUsbUpdateModifier(Report, KEY_MODIFIER_LSHIFT, Released);
	case ADB_KEY_LALT:
		return AkpUsbUpdateModifier(Report, KEY_MODIFIER_LALT, Released);
	case ADB_KEY_LWIN:
		return AkpUsbUpdateModifier(Report, KEY_MODIFIER_LWIN, Released);
	case ADB_KEY_RCTRL:
		return AkpUsbUpdateModifier(Report, KEY_MODIFIER_RCTRL, Released);
	case ADB_KEY_RSHIFT:
		return AkpUsbUpdateModifier(Report, KEY_MODIFIER_RSHIFT, Released);
	case ADB_KEY_RALT:
		return AkpUsbUpdateModifier(Report, KEY_MODIFIER_RALT, Released);
	case ADB_KEY_RWIN:
		return AkpUsbUpdateModifier(Report, KEY_MODIFIER_RWIN, Released);
	default:
		return false;
	}
}

static bool AkpConvertAdbUsb(PUSB_KBD_REPORT Report, UCHAR Layout, PUCHAR Data, UCHAR Offset, UCHAR Length) {
	// Power button ends up in both bytes
	if (Data[0] == Data[1] && (Data[0] & 0x80) == 0x7F) {
		return AkpUpdateKey(Report, sc_AdbToUsbTable[0x7F], (Data[0] & 0x80) != 0);
	}

	bool Changed = false;
	for (UCHAR i = Offset; i < Length; i++) {
		if (Data[i] == 0xFF) continue; // no key in this case
		UCHAR ScanCode = Data[i] & ~0x80;
		bool Released = (Data[i] & 0x80) != 0;
		bool ChangedThis = false;
		switch (Layout) {
		default:
			break;
		case ADB_LAYOUT_ISO:
			// scancodes 0x32 and 0x0A are swapped
			if (ScanCode == 0x32) ScanCode = 0x0A;
			else if (ScanCode == 0x0A) ScanCode = 0x32;
			// fall through
		case ADB_LAYOUT_JIS:
			// ISO and JIS: scancode 0x2A becomes 0x70
			if (ScanCode == 0x2A) ScanCode = 0x70;
			break;
		}
		if (AkpKeyIsModifier(ScanCode)) {
			ChangedThis = AkpUpdateModifiers(Report, ScanCode, Released);
		}
		else {
			// Not a modifier
			ChangedThis = AkpUpdateKey(Report, sc_AdbToUsbTable[ScanCode], Released);
		}
		Changed = Changed || ChangedThis;
	}
	return Changed;
}

static void AdbKbdReceivePart(UCHAR Layout, PUCHAR Data, UCHAR Offset, UCHAR Length) {
	BOOLEAN Changed = AkpConvertAdbUsb(&s_Report, Layout, Data, Offset, Length);

	if (Changed) {
		KBDOnEvent(&s_Report);
	}
}


typedef struct adb_kbd_t adb_kbd_t;
struct adb_kbd_t {
    int next_key;
    char sequence[ADB_MAX_SEQUENCE_LEN];
    int len;
    char keytable[32];
};

static adb_dev_t *my_adb_dev = NULL;

static bool adb_kbd_poll(void) {
	uint8_t buffer[ADB_BUF_SIZE];
	adb_dev_t* dev = my_adb_dev;
	adb_kbd_t* kbd;
	int key;
	int ret;

	if (dev == NULL) return false;

	kbd = dev->state;

	/* Get saved state */
	ret = -1;
	if (adb_reg_get(dev, 0, buffer) != 2)
		return false;
	if ((buffer[0] & ~0x80) != (buffer[1] & ~0x80)) {
		AdbKbdReceivePart(dev->subType, buffer, 0, 2);
	}
	else {
		// Key-down and key-up for same key in same buffer, so split it up
		AdbKbdReceivePart(dev->subType, buffer, 0, 1);
		AdbKbdReceivePart(dev->subType, buffer, 1, 1);
	}

	return ret != 0;
}


void* adb_kbd_new(char* path, void* private)
{
	char buf[64];
	adb_kbd_t* kbd;
	adb_dev_t* dev = private;
	kbd = (adb_kbd_t*)malloc(sizeof(adb_kbd_t));
	if (kbd != NULL) {
		memset(kbd, 0, sizeof(adb_kbd_t));
		kbd->next_key = -1;
		kbd->len = 0;

		dev->state = kbd;
		dev->subType = AkpGetKeyboardLayout(dev->subType);
		my_adb_dev = dev;
	}

	return kbd;
}