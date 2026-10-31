#!/usr/bin/env python3
"""Connect an MCP client to a game that may not be running yet.

The game serves MCP over HTTP (run it with --mcp). An MCP client that can't connect when it
starts doesn't try again, so a server inside the game would be missing from any session that
began before the game did. This stays connected instead: it speaks stdio to the client and
forwards to the game, and with no game running, tools report that as an ordinary tool error.

It needs nothing beyond the standard library, and does nothing at startup but read stdin.

FINEVOX_MCP_PORT picks the port, 7801 by default.
"""
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

PORT = int(os.environ.get("FINEVOX_MCP_PORT", "7801"))
URL = f"http://127.0.0.1:{PORT}/mcp"
CACHE = Path.home() / ".cache" / "finevox" / f"mcp_tools_{PORT}.json"
CALL_TIMEOUT = 60
PROBE_TIMEOUT = 1
PROBE_SECONDS = 2

# Until a game has been seen, so that the client has something to call and be told what to do
PLACEHOLDER = [{
    "name": "status",
    "description": "Is a game running? Start one with --mcp to get the rest of the tools.",
    "inputSchema": {"type": "object", "properties": {}},
}]
NOT_RUNNING = f"No game is listening on 127.0.0.1:{PORT}. Start one with --mcp."

write_lock = threading.Lock()
tools_lock = threading.Lock()
known_tools = None


def send(message):
    with write_lock:
        sys.stdout.write(json.dumps(message) + "\n")
        sys.stdout.flush()


def forward(message, timeout):
    """The game's reply, or None if there's no game to ask."""
    request = urllib.request.Request(
        URL, data=json.dumps(message).encode(), headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as reply:
            body = reply.read()
    except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
        return None
    return json.loads(body) if body else {}


def load_cache():
    try:
        return json.loads(CACHE.read_text())
    except (OSError, ValueError):
        return None


def save_cache(tools):
    try:
        CACHE.parent.mkdir(parents=True, exist_ok=True)
        CACHE.write_text(json.dumps(tools))
    except OSError:
        pass


def current_tools():
    global known_tools
    with tools_lock:
        if known_tools is None:
            known_tools = load_cache() or PLACEHOLDER
        return known_tools


def probe_loop():
    """Tell the client when the tools change, as when a windowed game replaces a headless one."""
    global known_tools
    probe = {"jsonrpc": "2.0", "id": 0, "method": "tools/list"}
    while True:
        time.sleep(PROBE_SECONDS)
        reply = forward(probe, PROBE_TIMEOUT)
        if not reply or "result" not in reply:
            continue
        tools = reply["result"]["tools"]
        with tools_lock:
            changed = tools != known_tools
            known_tools = tools
        if changed:
            save_cache(tools)
            send({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})


def tool_error(request_id, text):
    return {"jsonrpc": "2.0", "id": request_id,
            "result": {"content": [{"type": "text", "text": text}], "isError": True}}


def handle(message):
    method = message.get("method")
    request_id = message.get("id")
    if request_id is None:
        return None
    if method == "initialize":
        version = message.get("params", {}).get("protocolVersion", "2025-06-18")
        return {"jsonrpc": "2.0", "id": request_id, "result": {
            "protocolVersion": version,
            "capabilities": {"tools": {"listChanged": True}},
            "serverInfo": {"name": "finevox-bridge", "version": "1"},
            "instructions": "A live game, if one is running with --mcp. Start with `status`. "
                            "server_* tools see the whole simulation; client_* tools see what "
                            "the player sees, fog of war included. *_eval runs finescript.",
        }}
    if method == "ping":
        return {"jsonrpc": "2.0", "id": request_id, "result": {}}
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": request_id, "result": {"tools": current_tools()}}
    if method == "tools/call":
        reply = forward(message, CALL_TIMEOUT)
        return reply if reply is not None else tool_error(request_id, NOT_RUNNING)
    return {"jsonrpc": "2.0", "id": request_id,
            "error": {"code": -32601, "message": f"no such method: {method}"}}


def main():
    threading.Thread(target=probe_loop, daemon=True).start()
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            message = json.loads(line)
        except ValueError:
            send({"jsonrpc": "2.0", "id": None,
                  "error": {"code": -32700, "message": "not JSON"}})
            continue
        # A slow tool mustn't hold up the rest: the client may send more meanwhile
        def run(message=message):
            reply = handle(message)
            if reply is not None:
                send(reply)
        threading.Thread(target=run, daemon=True).start()


if __name__ == "__main__":
    main()
