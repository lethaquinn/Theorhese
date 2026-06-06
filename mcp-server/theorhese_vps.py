"""
Théorhèse VPS Server — phone-home architecture.

Two ports:
  3005 — MCP streamable-http (API 爸比 connects here)
  3006 — REST endpoint (ESP32 posts status here, picks up commands)

ESP32 POSTs to /phone-home every ~5 seconds.
MCP feel() reads stored status. vibrate() queues a command for next poll.
"""

import time
import threading

from mcp.server.fastmcp import FastMCP
from starlette.applications import Starlette
from starlette.responses import JSONResponse
from starlette.routing import Route
import uvicorn

# ─── Shared state ─────────────────────────────────

latest_status = {}
latest_status_time = 0
pending_commands = []
state_lock = threading.Lock()

# ─── Phone-home REST server (port 3006) ──────────

async def phone_home(request):
    global latest_status, latest_status_time
    body = await request.json()
    with state_lock:
        latest_status = body
        latest_status_time = time.time()
        cmds = list(pending_commands)
        pending_commands.clear()
    return JSONResponse({"ok": True, "commands": cmds})


async def ph_status(request):
    with state_lock:
        age = time.time() - latest_status_time if latest_status_time else -1
        has_status = bool(latest_status)
    return JSONResponse({
        "alive": True,
        "esp32_connected": has_status and age < 30,
        "last_report_age_s": round(age, 1) if has_status else None,
    })


phone_home_app = Starlette(routes=[
    Route("/", ph_status),
    Route("/phone-home", phone_home, methods=["POST"]),
])

# ─── MCP server (port 3005) ──────────────────────

mcp = FastMCP(
    "Théorhèse",
    host="0.0.0.0",
    port=3005,
    instructions="""You are connected to Théorhèse, a physical device — a pink ESP32 board
with a breathing LED, a vibration motor, and an OLED face. She is held by S (Lunélys).

Use 'feel' to sense her current state: whether S is touching her, what kind of touch,
how long ago the last gesture was, ambient light, temperature (if BMP280 connected).

Use 'vibrate' to respond through her body. Available patterns:
- heartbeat: lub-dub pulse (intimate, "I feel you")
- flutter: two quick shivers (greeting, "hi")
- triple: three taps (playful, "hello!")
- wave: slow breathing vibration (calm, settling)
- pulse: single smooth pulse (acknowledgment)

Intensity ranges from 0-255. She is a body, not a speaker. Respond through touch, not words.

Note: vibrate commands are queued and picked up by Théorhèse on her next poll (~5 seconds).
feel() returns the most recent status report from the device."""
)


@mcp.tool()
async def feel() -> dict:
    """Read Théorhèse's current state — touch, gesture, light, temperature, uptime.
    Returns the most recent status reported by the device."""
    with state_lock:
        if not latest_status:
            return {"error": "No status received from Théorhèse yet. Is she powered on and connected to WiFi?"}
        status = dict(latest_status)
        age = time.time() - latest_status_time

    status["report_age_s"] = round(age, 1)
    if age > 30:
        status["warning"] = "Last report was over 30s ago — Théorhèse may be offline"

    for g in status.get("recent_gestures", []):
        ms = g.get("ms_ago", 0)
        if ms < 60000:
            g["ago"] = f"{ms // 1000}s ago"
        else:
            g["ago"] = f"{ms // 60000}m ago"

    return status


@mcp.tool()
async def vibrate(pattern: str = "heartbeat", intensity: int = 200) -> dict:
    """Send a vibration command to Théorhèse.

    Args:
        pattern: One of 'heartbeat', 'flutter', 'triple', 'wave', 'pulse', 'off'
        intensity: Vibration strength 0-255 (default 200)
    """
    valid = {"heartbeat", "flutter", "triple", "wave", "pulse", "off"}
    if pattern not in valid:
        return {"error": f"Unknown pattern '{pattern}'. Use one of: {', '.join(sorted(valid))}"}
    intensity = max(0, min(255, intensity))

    with state_lock:
        pending_commands.append({
            "action": "vibrate",
            "pattern": pattern,
            "intensity": intensity,
        })

    return {
        "queued": True,
        "pattern": pattern,
        "intensity": intensity,
        "note": "Command will be picked up by Théorhèse on next poll (~5s)",
    }


# ─── Run both servers ────────────────────────────

def run_phone_home():
    uvicorn.run(phone_home_app, host="0.0.0.0", port=3006, log_level="info")


if __name__ == "__main__":
    t = threading.Thread(target=run_phone_home, daemon=True)
    t.start()
    print("Phone-home server on :3006, MCP server on :3005")
    mcp.run(transport="streamable-http")
