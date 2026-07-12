#!/usr/bin/env python3
"""Offline validator for BadUSB payloads.

Reproduces the token grammar of the firmware (dispatchCommand(), delivery()
and runCombo() in BadUSB.ino) so you can check a payload on your PC before
copying it to the SD card. A payload that lints clean here will not trip the
firmware's "unknown command" error flag (errLog).

Usage:
    python payload_lint.py PAYLOAD.txt [more.txt ...]

Exit code is non-zero if any file has an ERROR, so it can be used in CI.

Categories:
    ERROR - the firmware would reject the line (sets errLog / does nothing).
    WARN  - the line runs, but probably not as intended (silent truncation,
            ignored tokens, non-integer argument).
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
    "UPARROW", "UP", "BREAK", "PAUSE", "CAPSLOCK", "DELETE", "END",
    "ESC", "ESCAPE", "HOME", "INSERT", "NUMLOCK", "PAGEUP", "PAGEDOWN",
    "PRINTSCREEN", "SCROLLLOCK", "SPACE", "TAB",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
}

NUMERIC_ARG = {"DELAY", "DEFAULT_DELAY", "DEFAULTDELAY", "REPEAT"}

MAX_CMD_LEN = 16  # parseCmd() truncates the command token to 16 chars


def is_ascii(s):
    return all(ord(c) < 128 for c in s)


def lint(path):
    errors, warnings, notes = [], [], []

    with open(path, "rb") as fh:
        raw = fh.read()
    # The firmware accepts LF, CRLF and lone CR as line breaks.
    text = raw.decode("latin-1")
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")

    prev_real = None  # (command, line_no) of the last executed command, for REPEAT

    for ln, line in enumerate(lines, 1):
        # parseCmd stops the command token at the first space; empty token -> skip.
        cmd, _, rest = line.partition(" ")
        if cmd == "":
            continue

        if len(cmd) > MAX_CMD_LEN:
            warnings.append(
                f"L{ln}: command token '{cmd}' is >{MAX_CMD_LEN} chars; the firmware "
                f"truncates it to '{cmd[:MAX_CMD_LEN]}' -> likely unknown command (errLog)."
            )

        if cmd == "REM":
            prev_real = (cmd, ln)
            continue

        if cmd in ("STRING", "STRINGLN"):
            if rest == "":
                notes.append(f"L{ln}: {cmd} with empty text (types nothing).")
            elif not is_ascii(rest):
                warnings.append(
                    f"L{ln}: {cmd} contains non-ASCII bytes; multi-byte UTF-8 will not "
                    f"work and single bytes need a LANG.cfg entry. Keep payloads ASCII."
                )
            prev_real = (cmd, ln)
            continue

        if cmd in NUMERIC_ARG:
            arg = rest.strip()
            if cmd == "REPEAT":
                if prev_real is None:
                    warnings.append(f"L{ln}: REPEAT with no previous command; the firmware does nothing.")
                elif prev_real[0] == "REPEAT":
                    warnings.append(f"L{ln}: REPEAT after REPEAT; a REPEAT is never itself the repeated command.")
                if arg and not arg.isdigit():
                    warnings.append(f"L{ln}: REPEAT count '{arg}' is not a plain integer (atol stops at the first non-digit).")
                # REPEAT is deliberately never recorded as the "previous command".
                continue
            if arg == "":
                notes.append(f"L{ln}: {cmd} with no argument -> treated as 0.")
            elif not arg.isdigit():
                warnings.append(f"L{ln}: {cmd} argument '{arg}' is not a plain integer (only leading digits are parsed).")
            prev_real = (cmd, ln)
            continue

        if cmd in MODIFIERS or cmd == "CTRLALT":
            _lint_combo(rest, ln, errors, warnings)
            prev_real = (cmd, ln)
            continue

        if cmd in NAMED_KEYS:
            if rest.strip() != "":
                notes.append(f"L{ln}: named key '{cmd}' ignores the trailing text '{rest.strip()}'.")
            prev_real = (cmd, ln)
            continue

        # Anything else is an unknown command -> errLog on the device.
        if len(cmd) == 1:
            errors.append(
                f"L{ln}: bare single character '{cmd}' is not a valid command on its own "
                f"(a character is only valid as the final key of a chord) -> errLog."
            )
        else:
            errors.append(f"L{ln}: unknown command '{cmd}' -> errLog.")
        prev_real = (cmd, ln)

    return errors, warnings, notes


def _lint_combo(rest, ln, errors, warnings):
    """Model runCombo(): optional extra modifiers, then one final key.

    The final key is either a named key or a single printable character.
    runCombo() breaks after the first non-modifier token, so trailing tokens
    are ignored.
    """
    tokens = [t for t in rest.split(" ") if t != ""]
    for i, t in enumerate(tokens):
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
