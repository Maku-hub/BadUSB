#!/usr/bin/env python3
"""Offline validator for BadUSB payloads.

Reproduces the token grammar of the firmware (parseCmd(), dispatchCommand(),
delivery() and runCombo() in BadUSB.ino) so you can check a payload on your PC
before copying it to the SD card. A payload that lints clean here will not trip
the firmware's "unknown command" error flag (errLog).

The parser is token-accurate, mirroring these firmware behaviors:
  * parseCmd() splits on single spaces and treats LF, CRLF and lone CR as line
    ends; it stores at most 16 chars of a token (extra chars are discarded).
  * Leading spaces before a command are skipped (parseCmd returns an empty token
    that delivery() ignores, then the command is parsed from the next non-space).
  * REM / STRING / STRINGLN / DELAY / DEFAULT_DELAY / DEFAULTDELAY / REPEAT and
    modifier chords (CTRL, ALT, ... , CTRLALT) CONSUME the rest of the line.
  * A named key (ENTER, TAB, F5, arrows, ...) and an unknown token consume ONLY
    themselves: any following tokens on the same line are parsed as further
    commands. So "TAB TAB" presses Tab twice and "ENTER foo" presses Enter then
    treats 'foo' as an unknown command (errLog).
  * STRING / STRINGLN with no argument on their own line make cmdString() read
    the FOLLOWING line as the text to type, consuming it.

Usage:
    python payload_lint.py PAYLOAD.txt [more.txt ...]

Exit code is non-zero if any file has an ERROR, so it can be used in CI.

Categories:
    ERROR - the firmware would reject the token (sets errLog / does nothing).
    WARN  - the line runs, but probably not as intended (silent truncation,
            ignored tokens, non-integer argument, a line consumed as text).
    note  - harmless, informational.
"""
import sys
import os

# Kept in sync with BadUSB.ino: modifierFor(), the keyCommands[] table and the
# command names handled by dispatchCommand()/delivery().
MODIFIERS = {"CTRL", "CONTROL", "ALT", "SHIFT", "GUI", "WINDOWS"}

NAMED_KEYS = {
    "ENTER", "MENU", "APP",
    "DOWNARROW", "DOWN", "LEFTARROW", "LEFT", "RIGHTARROW", "RIGHT",
    "UPARROW", "UP", "BREAK", "PAUSE", "CAPSLOCK", "BACKSPACE", "DELETE", "END",
    "ESC", "ESCAPE", "HOME", "INSERT", "NUMLOCK", "PAGEUP", "PAGEDOWN",
    "PRINTSCREEN", "SCROLLLOCK", "SPACE", "TAB",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
}

# Commands whose argument/text/chord makes dispatchCommand() (or delivery(), for
# REPEAT) consume the remainder of the line.
LINE_CONSUMING = {"REM", "STRING", "STRINGLN", "DELAY", "DEFAULT_DELAY",
                  "DEFAULTDELAY", "REPEAT"} | MODIFIERS | {"CTRLALT"}

MAX_CMD_LEN = 16  # parseCmd() truncates each token to 16 chars


def is_ascii(s):
    return all(ord(c) < 128 for c in s)


def lint(path):
    errors, warnings, notes = [], [], []

    with open(path, "rb") as fh:
        raw = fh.read()
    # The firmware accepts LF, CRLF and lone CR as line breaks.
    text = raw.decode("latin-1")
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")

    prev_real = [None]  # (command, line_no) of the last executed command, for REPEAT
    skip_next = False   # set when a STRING/STRINGLN consumed the following line

    for idx, line in enumerate(lines):
        if skip_next:
            skip_next = False
            continue
        ln = idx + 1
        has_next = idx + 1 < len(lines)
        skip_next = _lint_line(line, ln, has_next, errors, warnings, notes, prev_real)

    return errors, warnings, notes


def _lint_line(line, ln, has_next, errors, warnings, notes, prev_real):
    """Model parseCmd()/dispatchCommand() for one physical line.

    Returns True if the FOLLOWING line will be consumed as STRING/STRINGLN text.
    Updates prev_real[0] to (command, ln) for the last executed command.
    """
    n = len(line)
    pos = 0
    while pos < n:
        # Firmware skips space delimiters between command tokens.
        while pos < n and line[pos] == " ":
            pos += 1
        if pos >= n:
            break

        start = pos
        while pos < n and line[pos] != " ":
            pos += 1
        token = line[start:pos]
        broke_on_space = pos < n  # a space (and possibly more tokens/args) follows

        # parseCmd() keeps at most 16 chars of the token.
        if len(token) > MAX_CMD_LEN:
            warnings.append(
                f"L{ln}: command token '{token}' is >{MAX_CMD_LEN} chars; the firmware "
                f"truncates it to '{token[:MAX_CMD_LEN]}' -> likely unknown command (errLog)."
            )
            token = token[:MAX_CMD_LEN]

        # ---------- commands that CONSUME the rest of the line ----------
        if token == "REM":
            prev_real[0] = (token, ln)
            return False

        if token in ("STRING", "STRINGLN"):
            prev_real[0] = (token, ln)
            if broke_on_space:
                text = line[pos + 1:]  # everything after the single delimiter space
                if text == "":
                    notes.append(f"L{ln}: {token} types an empty string (only a delimiter space).")
                elif not is_ascii(text):
                    warnings.append(
                        f"L{ln}: {token} contains non-ASCII bytes; multi-byte UTF-8 will not "
                        f"work and single bytes need a LANG.cfg entry. Keep payloads ASCII."
                    )
                return False
            # No argument on this line: cmdString() reads the NEXT line as its text.
            if has_next:
                warnings.append(
                    f"L{ln}: {token} has no argument on its own line; the firmware types the "
                    f"FOLLOWING line (L{ln + 1}) as its text and consumes it."
                )
                return True
            notes.append(f"L{ln}: {token} at end of file with no text (types nothing).")
            return False

        if token in ("DELAY", "DEFAULT_DELAY", "DEFAULTDELAY"):
            arg = line[pos + 1:].strip() if broke_on_space else ""
            if arg == "":
                notes.append(f"L{ln}: {token} with no argument -> treated as 0.")
            elif not arg.isdigit():
                warnings.append(f"L{ln}: {token} argument '{arg}' is not a plain integer (only leading digits are parsed).")
            prev_real[0] = (token, ln)
            return False

        if token == "REPEAT":
            arg = line[pos + 1:].strip() if broke_on_space else ""
            if prev_real[0] is None:
                warnings.append(f"L{ln}: REPEAT with no previous command; the firmware does nothing.")
            elif prev_real[0][0] == "REPEAT":
                warnings.append(f"L{ln}: REPEAT after REPEAT; a REPEAT is never itself the repeated command.")
            if arg and not arg.isdigit():
                warnings.append(f"L{ln}: REPEAT count '{arg}' is not a plain integer (atol stops at the first non-digit).")
            # REPEAT is deliberately never recorded as the "previous command".
            return False

        if token in MODIFIERS or token == "CTRLALT":
            # runCombo() consumes the rest of the line as the chord.
            _lint_combo(line[pos:], ln, errors, warnings)
            prev_real[0] = (token, ln)
            return False

        # ---------- commands that consume ONLY this token ----------
        if token in NAMED_KEYS:
            prev_real[0] = (token, ln)
            continue  # firmware keeps parsing further tokens on this line

        # Unknown token -> errLog; the firmware does not consume the rest either,
        # so any following tokens are parsed as their own commands.
        if len(token) == 1:
            errors.append(
                f"L{ln}: bare single character '{token}' is not a valid command on its own "
                f"(a character is only valid as the final key of a chord) -> errLog."
            )
        else:
            errors.append(f"L{ln}: unknown command '{token}' -> errLog.")
        prev_real[0] = (token, ln)
        # continue with the next token on the line

    return False


def _lint_combo(rest, ln, errors, warnings):
    """Model runCombo(): optional extra modifiers, then one final key.

    The final key is either a named key or a single printable character.
    runCombo() breaks after the first non-modifier token, so trailing tokens
    are ignored. `rest` is the line content after the first modifier token.
    """
    tokens = [t for t in rest.split(" ") if t != ""]
    for i, t in enumerate(tokens):
        # parseCmd() truncates each chord token to 16 chars too.
        if len(t) > MAX_CMD_LEN:
            t = t[:MAX_CMD_LEN]
        if t in MODIFIERS:
            continue  # another modifier added to the chord
        # First non-modifier token is the final key.
        if t not in NAMED_KEYS and len(t) != 1:
            errors.append(
                f"L{ln}: chord final key '{t}' is neither a named key nor a single character -> errLog."
            )
        trailing = tokens[i + 1:]
        if trailing:
            warnings.append(f"L{ln}: tokens after the chord's final key are ignored: {' '.join(trailing)}")
        return
    # No non-modifier token: a bare modifier tap (e.g. "GUI") or all-modifier chord. Valid.


def main(argv):
    paths = argv[1:]
    if not paths:
        print(__doc__)
        return 2

    total_errors = 0
    for p in paths:
        if not os.path.isfile(p):
            print(f"\n=== {p} : FAIL (file not found) ===")
            total_errors += 1
            continue
        errors, warnings, notes = lint(p)
        status = "OK" if not errors else "FAIL"
        print(f"\n=== {os.path.basename(p)} : {status} "
              f"({len(errors)} error, {len(warnings)} warn, {len(notes)} note) ===")
        for e in errors:
            print("  ERROR ", e)
        for w in warnings:
            print("  WARN  ", w)
        for n in notes:
            print("  note  ", n)
        total_errors += len(errors)

    print(f"\nTotal errors across {len(paths)} file(s): {total_errors}")
    return 1 if total_errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
