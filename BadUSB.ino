// CJMCU-32 keyboard emulator based on the ATMEGA32U4 chip.
//
// Reads a DuckyScript-like payload from a microSD card and replays it as USB
// HID keystrokes. See README.md for the payload syntax and SD card layout.

#include <Keyboard.h>
#include <SPI.h>
#include <SD.h>
#include "keys.h"
#include "keymap.h"

// ---- Hardware configuration -------------------------------------------------
const int chipSelect = 4; // SPI chip-select of the SD reader
const int ledStatus  = 8; // status LED (payload finished / error blink)

// ---- Runtime state ----------------------------------------------------------
long defaultDelay = 0;     // pause inserted before every command (ms)
bool errLog       = false; // set on an unknown command during delivery()

char cmd[17];              // current command token (max 16 chars + '\0')
char breakChar;            // delimiter that ended the last parseCmd() (' ' or '\n')

// Error flag persisted on the SD card: delivery() creates it on an unknown
// command; management() reads it (blinks the LED) and clears it. This survives
// the reboot between auto-disarm delivery and the following management run.
const char        *errFlagFile = "err.flg";
const unsigned long NO_POSITION = 0xFFFFFFFFUL; // "no previous command" sentinel

// ---- Forward declarations ---------------------------------------------------
void   delivery(const String &fileName);
void   management(const String &mode, const String &payload);
void   printDirectory(File dir, int numTabs);
String inputData();
String readConfig(const char *fileName);
void   writeConfig(const char *fileName, const String &data);

void   sendChar(byte out, byte modifier);
void   cmdPressKey(byte key);
void   cmdString(File &f);
byte   modifierFor(const char *token);
void   runCombo(File &f, byte mod1, byte mod2);
void   dispatchCommand(File &f);

void   skipToEol(File &f);
long   readArgLong(File &f);
void   parseCmd(File &f);
bool   lookupKey(const char *name, byte *out);

// ---- Simple single-key command table ----------------------------------------
// Commands that just press one key. Keeping them in a table (instead of a long
// if/else chain) makes delivery() short and adding a key a one-line change.
struct KeyCommand {
  const char *name;
  byte        key;
};

static const KeyCommand keyCommands[] = {
  {"ENTER", KEY_RETURN},
  {"MENU", KEY_MENU}, {"APP", KEY_MENU},
  {"DOWNARROW", KEY_DOWN_ARROW},   {"DOWN", KEY_DOWN_ARROW},
  {"LEFTARROW", KEY_LEFT_ARROW},   {"LEFT", KEY_LEFT_ARROW},
  {"RIGHTARROW", KEY_RIGHT_ARROW}, {"RIGHT", KEY_RIGHT_ARROW},
  {"UPARROW", KEY_UP_ARROW},       {"UP", KEY_UP_ARROW},
  {"BREAK", KEY_BREAK}, {"PAUSE", KEY_BREAK},
  {"CAPSLOCK", KEY_CAPS_LOCK},
  {"BACKSPACE", KEY_BACKSPACE},
  {"DELETE", KEY_DELETE},
  {"END", KEY_END},
  {"ESC", KEY_ESC}, {"ESCAPE", KEY_ESC},
  {"HOME", KEY_HOME},
  {"INSERT", KEY_INSERT},
  {"NUMLOCK", KEY_NUMLOCK},
  {"PAGEUP", KEY_PAGE_UP},
  {"PAGEDOWN", KEY_PAGE_DOWN},
  {"PRINTSCREEN", KEY_PRINTSCREEN},
  {"SCROLLLOCK", KEY_SCROLLLOCK},
  {"SPACE", KEY_SPACE},
  {"TAB", KEY_TAB},
  {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3},  {"F4", KEY_F4},
  {"F5", KEY_F5}, {"F6", KEY_F6}, {"F7", KEY_F7},  {"F8", KEY_F8},
  {"F9", KEY_F9}, {"F10", KEY_F10}, {"F11", KEY_F11}, {"F12", KEY_F12},
};

bool lookupKey(const char *name, byte *out) {
  for (uint8_t i = 0; i < sizeof(keyCommands) / sizeof(keyCommands[0]); i++) {
    if (strcmp(name, keyCommands[i].name) == 0) {
      *out = keyCommands[i].key;
      return true;
    }
  }
  return false;
}

// ---- Arduino entry points ---------------------------------------------------
void setup() {
  pinMode(ledStatus, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  Keyboard.begin();
  Serial.begin(9600);

  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    return;
  }

  loadKeymap("lang.cfg");

  String mode    = readConfig("mode.cfg");
  String payload = readConfig("exec.cfg");

  bool deliverMode = (mode == "c" || mode == "a");
  if (deliverMode && payload != "") {
    delivery(payload);
    if (mode == "a") {           // auto-disarm: run once, then management next boot
      writeConfig("mode.cfg", "m");
    }
  } else {
    // Management mode ("m"), an unknown/empty mode, or a missing payload all
    // land here, so the device can always be (re)configured over serial instead
    // of ending up inert.
    if (payload == "") {
      Serial.println("No payload configured (exec.cfg missing or empty)");
    }
    management(mode, payload);
  }

  Keyboard.end();
}

void loop() {
}

// ---- Management mode --------------------------------------------------------
void management(const String &mode, const String &payload) {
  digitalWrite(ledStatus, HIGH);

  bool hadError = SD.exists(errFlagFile);

  // Wait for the host to open the serial monitor; blink if the previous
  // delivery hit an unknown command.
  while (!Serial) {
    if (hadError) {
      digitalWrite(ledStatus, HIGH);
      delay(200);
      digitalWrite(ledStatus, LOW);
      delay(200);
    }
  }

  SD.remove(errFlagFile); // error acknowledged now that a monitor is attached

  File root = SD.open("/");
  Serial.println("Available payloads:");
  printDirectory(root, 0);
  root.close();

  Serial.println();
  Serial.println("Available modes:");
  Serial.println("  m => management mode");
  Serial.println("  a => auto-disarm mode");
  Serial.println("  c => continuous delivery mode");
  Serial.println();
  Serial.print("Current mode: ");
  Serial.println(mode);
  Serial.print("Current payload: ");
  Serial.println(payload);
  Serial.println();

  Serial.println("Input mode:");
  writeConfig("mode.cfg", inputData());
  Serial.println();
  Serial.println("Input payload:");
  writeConfig("exec.cfg", inputData());
  Serial.println();
}

void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    for (int i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

// Blocks until a full line arrives on the serial port; trims trailing CR/space.
String inputData() {
  while (Serial.available() == 0) {
  }
  String s = Serial.readStringUntil('\n');
  s.trim();
  return s;
}

// ---- SD config helpers ------------------------------------------------------
String readConfig(const char *fileName) {
  File f = SD.open(fileName);
  if (!f) {
    Serial.print("Error opening ");
    Serial.println(fileName);
    return "";
  }
  String content;
  while (f.available()) {
    content += (char) f.read();
  }
  f.close();
  content.trim();
  return content;
}

void writeConfig(const char *fileName, const String &data) {
  SD.remove(fileName);
  File f = SD.open(fileName, FILE_WRITE);
  if (f) {
    f.print(data);
    f.close();
  }
}

// ---- HID output -------------------------------------------------------------
// HID timing. Too-short values make the host drop keys or merge USB reports
// (lost characters, stuck SHIFT); overly long values just make typing slow.
//   charDelayMs - press/release spacing while typing STRING characters.
//   keyHoldMs   - how long a named key / chord is held (kept well under the
//                 host key-repeat threshold so keys never auto-repeat).
//   settleMs    - pause after releaseAll() so the key/modifier release is seen
//                 before the next command runs (prevents a modifier from a chord
//                 bleeding into the following key, e.g. LEFT acting as SHIFT+LEFT).
const int charDelayMs = 12;
const int keyHoldMs   = 30;
const int settleMs    = 40;

// Press+release a printable character, optionally holding a modifier. Uses
// explicit press/release with a gap (instead of Keyboard.write back-to-back) so
// the host reliably registers each key even in a long STRING.
void sendChar(byte out, byte modifier) {
  if (modifier) {
    Keyboard.press(modifier);
    delay(charDelayMs);
    Keyboard.press(out);
    delay(charDelayMs);
    Keyboard.releaseAll();
  } else {
    Keyboard.press(out);
    delay(charDelayMs);
    Keyboard.release(out);
  }
  delay(charDelayMs);
}

// Press+release a special key. Special keys bypass the keymap on purpose:
// their codes (0xB0-0xED) must never be remapped by LANG.cfg.
void cmdPressKey(byte key) {
  Keyboard.releaseAll(); // start from a clean report so no stray modifier rides
  delay(charDelayMs);    // along with this key (e.g. LEFT arriving as SHIFT+LEFT)
  Keyboard.press(key);
  delay(keyHoldMs);
  Keyboard.releaseAll();
  delay(settleMs); // let the release register before the next command
}

// Returns the modifier key code for a modifier keyword, or 0 if the token is
// not a modifier. Recognizes the DuckyScript modifier names.
byte modifierFor(const char *token) {
  if (strcmp(token, "CTRL") == 0 || strcmp(token, "CONTROL") == 0) return KEY_LEFT_CTRL;
  if (strcmp(token, "ALT") == 0)                                   return KEY_LEFT_ALT;
  if (strcmp(token, "SHIFT") == 0)                                 return KEY_LEFT_SHIFT;
  if (strcmp(token, "GUI") == 0 || strcmp(token, "WINDOWS") == 0)  return KEY_LEFT_GUI;
  return 0;
}

// Executes a modifier chord: holds the given modifier(s), then reads any
// further space-separated tokens on the line. Extra modifier keywords are
// added to the chord; the first non-modifier token is the final key - either a
// named key (ENTER, F4, DELETE, arrows, ...) or a single character. Matches
// DuckyScript combos such as "GUI r", "ALT F4" and "CTRL ALT DELETE".
void runCombo(File &f, byte mod1, byte mod2) {
  Keyboard.press(mod1);
  if (mod2) {
    Keyboard.press(mod2);
  }
  delay(keyHoldMs);

  while (breakChar == ' ') {  // more tokens follow on this line
    parseCmd(f);
    if (cmd[0] == '\0') {
      break;
    }

    byte mod = modifierFor(cmd);
    if (mod) {                // another modifier - add it and keep going
      Keyboard.press(mod);
      delay(keyHoldMs);
      continue;
    }

    // Final key of the chord.
    byte key;
    if (lookupKey(cmd, &key)) {          // named key
      Keyboard.press(key);
    } else if (cmd[1] == '\0') {         // single character (via keymap)
      byte charMod;
      byte out = convertLangChar((byte) cmd[0], &charMod);
      if (charMod) {
        Keyboard.press(charMod);
      }
      Keyboard.press(out);
    } else {
      errLog = true;                     // unknown key name in a chord
    }
    break;
  }

  delay(keyHoldMs);
  Keyboard.releaseAll();
  delay(settleMs); // let the release register so a modifier does not bleed on
}

// Types the rest of the current line (STRING) through the keymap.
void cmdString(File &f) {
  while (true) {
    int r = f.read();
    if (r < 0 || (char) r == '\n') { // EOF or LF end of line
      break;
    }
    char c = (char) r;
    if (c == '\r') {                 // CR / CRLF end of line
      if (f.peek() == '\n') {
        f.read();
      }
      break;
    }
    byte modifier;
    byte out = convertLangChar((byte) c, &modifier);
    sendChar(out, modifier);
  }
}

// ---- Payload parsing --------------------------------------------------------
// Consumes the remainder of the current line (used by REM and to drop the tail
// after a single-char argument). Handles LF, CRLF and lone CR.
void skipToEol(File &f) {
  while (true) {
    int r = f.read();
    if (r < 0 || (char) r == '\n') {
      break;
    }
    if ((char) r == '\r') {
      if (f.peek() == '\n') {
        f.read();
      }
      break;
    }
  }
}

// Reads the rest of the line as a non-negative integer (for DELAY-like args).
long readArgLong(File &f) {
  char buf[12];
  int  i = 0;
  while (true) {
    int r = f.read();
    if (r < 0 || (char) r == '\n') {
      break;
    }
    char c = (char) r;
    if (c == '\r') {
      if (f.peek() == '\n') {
        f.read();
      }
      break;
    }
    if (i < (int) sizeof(buf) - 1) {
      buf[i++] = c;
    }
  }
  buf[i] = '\0';
  long value = atol(buf);
  return value < 0 ? 0 : value; // never feed delay() a negative (huge) value
}

// Reads the next command token into cmd[]. Sets breakChar to the delimiter that
// ended it (' ' means an argument follows on the same line). Extra characters
// past the 16-char limit are discarded rather than left to be misparsed.
void parseCmd(File &f) {
  int len = 0;
  while (true) {
    int r = f.read();
    if (r < 0) {                 // EOF
      breakChar = '\n';
      break;
    }
    char c = (char) r;
    if (c == '\r') {             // treat CR / CRLF as end of line
      if (f.peek() == '\n') {
        f.read();
      }
      breakChar = '\n';
      break;
    }
    if (c == ' ' || c == '\n') {
      breakChar = c;
      break;
    }
    if (len < 16) {
      cmd[len++] = c;
    }
  }
  cmd[len] = '\0';
}

// Executes the command currently in cmd[] (already parsed by parseCmd). Any
// argument is read from f. Empty lines and REPEAT are handled by delivery().
void dispatchCommand(File &f) {
  byte mod;
  if (strcmp(cmd, "REM") == 0) {
    skipToEol(f);
  } else if (strcmp(cmd, "STRING") == 0) {
    cmdString(f);
  } else if (strcmp(cmd, "STRINGLN") == 0) {
    cmdString(f);
    cmdPressKey(KEY_RETURN);
  } else if (strcmp(cmd, "DELAY") == 0) {
    delay(breakChar == ' ' ? readArgLong(f) : 0);
  } else if (strcmp(cmd, "DEFAULT_DELAY") == 0 || strcmp(cmd, "DEFAULTDELAY") == 0) {
    defaultDelay = (breakChar == ' ') ? readArgLong(f) : 0;
  } else if ((mod = modifierFor(cmd)) != 0) {
    runCombo(f, mod, 0);
  } else if (strcmp(cmd, "CTRLALT") == 0) {
    runCombo(f, KEY_LEFT_CTRL, KEY_LEFT_ALT);
  } else {
    byte key;
    if (lookupKey(cmd, &key)) {
      cmdPressKey(key);
    } else {
      errLog = true; // unknown command
    }
  }
}

// ---- Payload execution ------------------------------------------------------
void delivery(const String &fileName) {
  delay(800);
  File dataFile = SD.open(fileName);
  if (!dataFile) {
    Serial.println("Error opening script file");
    return;
  }

  SD.remove(errFlagFile); // clear any stale flag from a previous run
  errLog = false;

  unsigned long prevLineStart = NO_POSITION; // start of the last real command

  while (dataFile.available()) {
    if (defaultDelay != 0) {
      delay(defaultDelay);
    }

    unsigned long lineStart = dataFile.position();
    parseCmd(dataFile);
    if (cmd[0] == '\0') {
      continue; // empty line
    }

    if (strcmp(cmd, "REPEAT") == 0) {
      long times = (breakChar == ' ') ? readArgLong(dataFile) : 0;
      unsigned long afterRepeat = dataFile.position();
      if (prevLineStart != NO_POSITION) {
        for (long k = 0; k < times; k++) {
          dataFile.seek(prevLineStart);
          parseCmd(dataFile);
          if (cmd[0] != '\0') {
            dispatchCommand(dataFile);
          }
        }
      }
      dataFile.seek(afterRepeat);
      continue; // REPEAT is never itself "the previous command"
    }

    dispatchCommand(dataFile);
    prevLineStart = lineStart;
  }

  dataFile.close();
  Keyboard.releaseAll();

  if (errLog) { // persist the error so management mode can signal it
    File flag = SD.open(errFlagFile, FILE_WRITE);
    if (flag) {
      flag.close();
    }
  }

  digitalWrite(ledStatus, HIGH);
  delay(500);
  digitalWrite(ledStatus, LOW);
}
