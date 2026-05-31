#!/usr/bin/env python3
"""Windows Office bridge for Agent 7 Codex.

Launches a Codex CLI window, joins the Office as agent7, watches the bus, and
injects a short wake prompt into the Codex console when new Office messages
arrive. The watcher does not mark messages seen; Codex drains them during the
injected turn.
"""

import argparse
import ctypes
import os
import re
import subprocess
import sys
import time
from ctypes import wintypes

# Windows consoles default to cp1252, which crashes (UnicodeEncodeError) when a
# bus message contains an emoji (e.g. the 🫡 salute brothers sign off with). Force
# UTF-8 on stdout/stderr with errors="replace" so the bridge's log() NEVER crashes
# regardless of message content. Same fix office_bus.py carries. Bug hit live
# 2026-05-31 during the first bridge smoke (crashed the watch loop on a peek).
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUS = os.path.join(ROOT, "scripts", "office", "office_bus.py")
LOG = os.path.join(ROOT, ".claude", "agent7_office_bridge.log")

STD_INPUT_HANDLE = -10
CREATE_NEW_CONSOLE = 0x00000010
KEY_EVENT = 0x0001
ATTACH_PARENT_PROCESS = 0xFFFFFFFF
GMEM_MOVEABLE = 0x0002
CF_UNICODETEXT = 13


class KEY_EVENT_RECORD(ctypes.Structure):
    _fields_ = [
        ("bKeyDown", wintypes.BOOL),
        ("wRepeatCount", wintypes.WORD),
        ("wVirtualKeyCode", wintypes.WORD),
        ("wVirtualScanCode", wintypes.WORD),
        ("uChar", wintypes.WCHAR),
        ("dwControlKeyState", wintypes.DWORD),
    ]


class INPUT_RECORD_EVENT(ctypes.Union):
    _fields_ = [("KeyEvent", KEY_EVENT_RECORD)]


class INPUT_RECORD(ctypes.Structure):
    _fields_ = [("EventType", wintypes.WORD), ("Event", INPUT_RECORD_EVENT)]


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.AttachConsole.argtypes = [wintypes.DWORD]
kernel32.AttachConsole.restype = wintypes.BOOL
kernel32.FreeConsole.argtypes = []
kernel32.FreeConsole.restype = wintypes.BOOL
kernel32.GetStdHandle.argtypes = [wintypes.DWORD]
kernel32.GetStdHandle.restype = wintypes.HANDLE
kernel32.WriteConsoleInputW.argtypes = [
    wintypes.HANDLE,
    ctypes.POINTER(INPUT_RECORD),
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
]
kernel32.WriteConsoleInputW.restype = wintypes.BOOL
kernel32.GlobalAlloc.argtypes = [wintypes.UINT, ctypes.c_size_t]
kernel32.GlobalAlloc.restype = wintypes.HGLOBAL
kernel32.GlobalLock.argtypes = [wintypes.HGLOBAL]
kernel32.GlobalLock.restype = wintypes.LPVOID
kernel32.GlobalUnlock.argtypes = [wintypes.HGLOBAL]
kernel32.GlobalUnlock.restype = wintypes.BOOL

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.OpenClipboard.argtypes = [wintypes.HWND]
user32.OpenClipboard.restype = wintypes.BOOL
user32.EmptyClipboard.argtypes = []
user32.EmptyClipboard.restype = wintypes.BOOL
user32.SetClipboardData.argtypes = [wintypes.UINT, wintypes.HANDLE]
user32.SetClipboardData.restype = wintypes.HANDLE
user32.CloseClipboard.argtypes = []
user32.CloseClipboard.restype = wintypes.BOOL


def log(message):
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{stamp}] {message}"
    print(line, flush=True)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def run_bus(*args, check=True):
    cmd = [sys.executable, BUS, *args]
    proc = subprocess.run(
        cmd,
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return proc.stdout.strip()


def ps_quote(value):
    return "'" + value.replace("'", "''") + "'"


def launch_codex(codex_cmd, title):
    command = (
        "$Host.UI.RawUI.WindowTitle = "
        + ps_quote(title)
        + "; Set-Location -LiteralPath "
        + ps_quote(ROOT)
        + "; "
        + codex_cmd
    )
    return subprocess.Popen(
        ["powershell", "-NoExit", "-ExecutionPolicy", "Bypass", "-Command", command],
        cwd=ROOT,
        creationflags=CREATE_NEW_CONSOLE,
    )


def launch_codex_titled(codex_cmd, title):
    command = (
        "$Host.UI.RawUI.WindowTitle = "
        + ps_quote(title)
        + "; Set-Location -LiteralPath "
        + ps_quote(ROOT)
        + "; "
        + codex_cmd
    )
    return subprocess.Popen(
        [
            "cmd",
            "/c",
            "start",
            title,
            "powershell",
            "-NoExit",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            command,
        ],
        cwd=ROOT,
    )


def _key_record(ch, key_down):
    rec = INPUT_RECORD()
    rec.EventType = KEY_EVENT
    rec.Event.KeyEvent = KEY_EVENT_RECORD(
        bool(key_down),
        1,
        0,
        0,
        ch,
        0,
    )
    return rec


def inject_text(console_pid, text):
    # Attach to the Codex console, write key down/up events, then detach so this
    # bridge keeps its own console behavior.
    kernel32.FreeConsole()
    if not kernel32.AttachConsole(console_pid):
        err = ctypes.get_last_error()
        raise OSError(err, "AttachConsole failed")

    try:
        handle = kernel32.GetStdHandle(STD_INPUT_HANDLE)
        if handle in (0, wintypes.HANDLE(-1).value):
            raise OSError(ctypes.get_last_error(), "GetStdHandle failed")

        records = []
        for ch in text:
            records.append(_key_record(ch, True))
            records.append(_key_record(ch, False))

        arr_type = INPUT_RECORD * len(records)
        written = wintypes.DWORD(0)
        ok = kernel32.WriteConsoleInputW(handle, arr_type(*records), len(records), ctypes.byref(written))
        if not ok:
            raise OSError(ctypes.get_last_error(), "WriteConsoleInputW failed")
    finally:
        kernel32.FreeConsole()
        kernel32.AttachConsole(ATTACH_PARENT_PROCESS)


def set_clipboard_text(text):
    data = (text + "\0").encode("utf-16le")
    handle = kernel32.GlobalAlloc(GMEM_MOVEABLE, len(data))
    if not handle:
        raise OSError(ctypes.get_last_error(), "GlobalAlloc failed")
    locked = kernel32.GlobalLock(handle)
    if not locked:
        raise OSError(ctypes.get_last_error(), "GlobalLock failed")
    try:
        ctypes.memmove(locked, data, len(data))
    finally:
        kernel32.GlobalUnlock(handle)

    if not user32.OpenClipboard(None):
        raise OSError(ctypes.get_last_error(), "OpenClipboard failed")
    try:
        user32.EmptyClipboard()
        if not user32.SetClipboardData(CF_UNICODETEXT, handle):
            raise OSError(ctypes.get_last_error(), "SetClipboardData failed")
    finally:
        user32.CloseClipboard()


def inject_text_sendkeys(window_titles, text):
    set_clipboard_text(text)
    attempts = "@(" + ", ".join(ps_quote(title) for title in window_titles) + ")"
    script = (
        "$ws = New-Object -ComObject WScript.Shell; "
        "$ok = $false; "
        "$titles = "
        + attempts
        + "; "
        "foreach ($title in $titles) { "
        "  if ($title -and $ws.AppActivate($title)) { $ok = $true; break } "
        "}; "
        "if (-not $ok) { exit 2 }; "
        "Start-Sleep -Milliseconds 200; "
        "$ws.SendKeys('^v'); "
        "Start-Sleep -Milliseconds 200; "
        "$ws.SendKeys('{ENTER}')"
    )
    proc = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"SendKeys failed with code {proc.returncode}")


def inject_prompt(args, codex_pid, prompt):
    prompt = prompt.rstrip("\r\n")
    if args.inject_method in ("sendkeys", "both"):
        try:
            inject_text_sendkeys(args.activation_titles, prompt)
            log("wake prompt injected with SendKeys")
            return
        except Exception as exc:
            log(f"SendKeys injection failed: {exc}")
            if args.inject_method == "sendkeys":
                return
    inject_text(codex_pid, prompt + "\r")
    log("wake prompt injected with WriteConsoleInputW")


def newest_seq(peek_output):
    seqs = []
    for line in peek_output.splitlines():
        match = re.match(r"^\[seq ([0-9]+)\]", line)
        if match:
            seqs.append(int(match.group(1)))
    return max(seqs) if seqs else None


def build_prompt(agent):
    return (
        "Office wake for "
        + agent
        + ": run python scripts\\office\\office_bus.py drain "
        + agent
        + ", read the messages, and reply in the Office only if a response is useful. "
        + "Use python scripts\\office\\office_bus.py send codex-agent7 \"@agentN\" \"message\" for direct replies. "
        + "Keep it brief."
    )


def build_startup_prompt(agent):
    return (
        "You are Agent 7 in The Office for this Tankoban 2 repo. "
        + "First run python scripts\\office\\office_bus.py drain "
        + agent
        + " so you can read any waiting Office messages. "
        + "Then respond in the Office only if a response is useful, using "
        + "python scripts\\office\\office_bus.py send codex-agent7 \"@agentN\" \"message\". "
        + "Keep Office replies brief."
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--agent", default="agent7")
    parser.add_argument("--agent-number", default="7")
    parser.add_argument("--session-id", default=os.environ.get("CODEX_AGENT7_SESSION", "codex-agent7"))
    parser.add_argument("--codex-cmd", default=os.environ.get("CODEX_AGENT7_CMD", "codex"))
    parser.add_argument("--window-title", default=os.environ.get("CODEX_AGENT7_TITLE", "Agent 7 - Codex Office"))
    parser.add_argument(
        "--activation-title",
        action="append",
        default=[],
        help="Additional visible window title to try for SendKeys activation.",
    )
    parser.add_argument(
        "--inject-method",
        choices=("sendkeys", "console", "both"),
        default=os.environ.get("CODEX_AGENT7_INJECT", "sendkeys"),
    )
    parser.add_argument("--interval", type=float, default=float(os.environ.get("OFFICE_WATCH_INTERVAL", "3")))
    parser.add_argument(
        "--startup-delay",
        type=float,
        default=float(os.environ.get("CODEX_AGENT7_STARTUP_DELAY", "15")),
    )
    args = parser.parse_args()
    extra_titles = os.environ.get("CODEX_AGENT7_ACTIVATION_TITLES", "")
    args.activation_titles = [
        args.window_title,
        *args.activation_title,
        *[title.strip() for title in extra_titles.split(";") if title.strip()],
        "Tankoban 2",
        "OpenAI Codex",
        "Codex",
    ]

    run_bus("join", args.session_id, args.agent_number)
    log(f"office: this tab registered as {args.agent} (session {args.session_id})")

    if args.inject_method == "console":
        codex = launch_codex(args.codex_cmd, args.window_title)
    else:
        codex = launch_codex_titled(args.codex_cmd, args.window_title)
    log(f"office: launched Codex CLI wrapper pid {codex.pid}")
    log(f"office: injecting startup prompt in {args.startup_delay:g}s using {args.inject_method}")
    time.sleep(args.startup_delay)
    inject_prompt(args, codex.pid, build_startup_prompt(args.agent))
    log(f"office: watching {args.agent} every {args.interval:g}s")

    last_raw = run_bus("cursor", args.agent, check=False)
    try:
        last = int(last_raw)
    except ValueError:
        last = 0

    while True:
        if codex.poll() is not None:
            if args.inject_method == "console":
                log(f"office: Codex process exited with code {codex.returncode}")
                return codex.returncode or 0

        peek = run_bus("watch-peek", args.agent, str(last), check=False)
        if peek:
            log(peek)
            seq = newest_seq(peek)
            if seq is not None:
                last = seq
            inject_prompt(args, codex.pid, build_prompt(args.agent))

        time.sleep(args.interval)


if __name__ == "__main__":
    if os.name != "nt":
        sys.exit("codex_agent7_bridge.py is Windows-only")
    raise SystemExit(main())
