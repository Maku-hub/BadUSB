#ifndef BADUSB_KEYS_H
#define BADUSB_KEYS_H

// Non-printing HID key codes used by the parser.
//
// Most of these are already provided by Arduino's Keyboard.h. Each define is
// guarded with #ifndef so that, when the library already defines a name, its
// value wins and we never trigger a redefinition error across core versions.
// The values below match the Arduino AVR core and cover the few keys the
// library may not define (KEY_MENU, KEY_SPACE, KEY_BREAK, ...).

#ifndef KEY_LEFT_CTRL
#define KEY_LEFT_CTRL 0x80
#endif
#ifndef KEY_LEFT_SHIFT
#define KEY_LEFT_SHIFT 0x81
#endif
#ifndef KEY_LEFT_ALT
#define KEY_LEFT_ALT 0x82
#endif
#ifndef KEY_LEFT_GUI
#define KEY_LEFT_GUI 0x83
#endif
#ifndef KEY_RIGHT_CTRL
#define KEY_RIGHT_CTRL 0x84
#endif
#ifndef KEY_RIGHT_SHIFT
#define KEY_RIGHT_SHIFT 0x85
#endif
#ifndef KEY_RIGHT_ALT
#define KEY_RIGHT_ALT 0x86
#endif
#ifndef KEY_RIGHT_GUI
#define KEY_RIGHT_GUI 0x87
#endif

#ifndef KEY_RETURN
#define KEY_RETURN 0xB0
#endif
#ifndef KEY_ESC
#define KEY_ESC 0xB1
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 0xB2
#endif
#ifndef KEY_TAB
#define KEY_TAB 0xB3
#endif
#ifndef KEY_SPACE
#define KEY_SPACE 0xB4
#endif

#ifndef KEY_CAPS_LOCK
#define KEY_CAPS_LOCK 0xC1
#endif
#ifndef KEY_F1
#define KEY_F1 0xC2
#endif
#ifndef KEY_F2
#define KEY_F2 0xC3
#endif
#ifndef KEY_F3
#define KEY_F3 0xC4
#endif
#ifndef KEY_F4
#define KEY_F4 0xC5
#endif
#ifndef KEY_F5
#define KEY_F5 0xC6
#endif
#ifndef KEY_F6
#define KEY_F6 0xC7
#endif
#ifndef KEY_F7
#define KEY_F7 0xC8
#endif
#ifndef KEY_F8
#define KEY_F8 0xC9
#endif
#ifndef KEY_F9
#define KEY_F9 0xCA
#endif
#ifndef KEY_F10
#define KEY_F10 0xCB
#endif
#ifndef KEY_F11
#define KEY_F11 0xCC
#endif
#ifndef KEY_F12
#define KEY_F12 0xCD
#endif

#ifndef KEY_PRINTSCREEN
#define KEY_PRINTSCREEN 0xCE
#endif
#ifndef KEY_SCROLLLOCK
#define KEY_SCROLLLOCK 0xCF
#endif
#ifndef KEY_BREAK
#define KEY_BREAK 0xD0
#endif
#ifndef KEY_INSERT
#define KEY_INSERT 0xD1
#endif
#ifndef KEY_HOME
#define KEY_HOME 0xD2
#endif
#ifndef KEY_PAGE_UP
#define KEY_PAGE_UP 0xD3
#endif
#ifndef KEY_DELETE
#define KEY_DELETE 0xD4
#endif
#ifndef KEY_END
#define KEY_END 0xD5
#endif
#ifndef KEY_PAGE_DOWN
#define KEY_PAGE_DOWN 0xD6
#endif
#ifndef KEY_RIGHT_ARROW
#define KEY_RIGHT_ARROW 0xD7
#endif
#ifndef KEY_LEFT_ARROW
#define KEY_LEFT_ARROW 0xD8
#endif
#ifndef KEY_DOWN_ARROW
#define KEY_DOWN_ARROW 0xD9
#endif
#ifndef KEY_UP_ARROW
#define KEY_UP_ARROW 0xDA
#endif
#ifndef KEY_NUMLOCK
#define KEY_NUMLOCK 0xDB
#endif
#ifndef KEY_MENU
#define KEY_MENU 0xED
#endif

#endif // BADUSB_KEYS_H
