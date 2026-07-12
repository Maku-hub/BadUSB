#ifndef BADUSB_KEYMAP_H
#define BADUSB_KEYMAP_H

#include <Arduino.h>

// Maximum number of remapping entries read from LANG.cfg.
#define KEYMAP_MAX 64

// Loads the optional character map (LANG.cfg) from the SD card.
// A missing file simply leaves the map empty (passthrough).
void loadKeymap(const char *fileName);

// Translates a single input byte using the loaded map.
// Returns the byte to send to the host and writes the accompanying modifier
// key (0 = none) to *modifierOut. With no matching entry it returns `in`
// unchanged, i.e. acts as a passthrough.
byte convertLangChar(byte in, byte *modifierOut);

#endif // BADUSB_KEYMAP_H
