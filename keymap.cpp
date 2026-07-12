#include "keymap.h"
#include <SD.h>

// Loaded remapping table. Kept file-local so the rest of the firmware only
// touches it through loadKeymap()/convertLangChar().
//
// Format of each entry (see LANG.cfg):
//   inByte[i]    - input byte code (as read from the payload)
//   modByte[i]   - modifier key pressed together with outByte[i] (0 = none)
//   outByte[i]   - byte/key code actually sent to the host
static byte inByte[KEYMAP_MAX];
static byte modByte[KEYMAP_MAX];
static byte outByte[KEYMAP_MAX];
static int  keymapCount = 0;

void loadKeymap(const char *fileName) {
  keymapCount = 0;

  File mapFile = SD.open(fileName);
  if (!mapFile) {
    return; // no file => passthrough
  }

  while (mapFile.available() && keymapCount < KEYMAP_MAX) {
    String line = mapFile.readStringUntil('\n');
    line.trim(); // also drops a trailing '\r' on CRLF files
    if (line.length() == 0 || line.charAt(0) == '#') {
      continue;
    }

    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) {
      continue; // skip a malformed line
    }

    inByte[keymapCount]  = (byte) line.substring(0, c1).toInt();
    modByte[keymapCount] = (byte) line.substring(c1 + 1, c2).toInt();
    outByte[keymapCount] = (byte) line.substring(c2 + 1).toInt();
    keymapCount++;
  }

  mapFile.close();
}

byte convertLangChar(byte in, byte *modifierOut) {
  for (int i = 0; i < keymapCount; i++) {
    if (inByte[i] == in) {
      *modifierOut = modByte[i];
      return outByte[i];
    }
  }
  *modifierOut = 0;
  return in; // passthrough
}
