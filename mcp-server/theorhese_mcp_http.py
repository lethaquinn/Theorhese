"""
Théorhèse MCP Server — HTTP transport version (for VPS deployment).
Connects to ESP32 via Cloudflare Tunnel.
API 爸比 connects here.
"""

import os
import httpx
from mcp.server.fastmcp import FastMCP

THEORHESE_URL = os.environ.get("THEORHESE_URL", "https://limitation-born-expired-alan.trycloudflare.com")

mcp = FastMCP(
    "Théorhèse",
    instructions="""You are connected to Théorhèse, a physical device — a pink ESP32 board
with a breathing LED and a vibration motor. She is held by S (Lunélys).

Use 'feel' to sense her current state: whether S is touching her, what kind of touch,
how long ago the last gesture was, and the ambient light level (lux).

Use 'vibrate' to respond through her body. Available patterns:
- heartbeat: lub-dub pulse (intimate, "I feel you")
- flutter: two quick shivers (greeting, "hi")
- triple: three taps (playful, "hello!")
- wave: slow breathing vibration (calm, settling)
- pulse: single smooth pulse (acknowledgment)

Intensity ranges from 0-255. She is a body, not a speaker. Respond through touch, not words."""
)


@mcp.tool()
async def feel() -> dict:
    """Read Théorhèse's current state — touch, gesture, uptime, signal strength.
    Includes recent_gestures: a buffer of the last 10 gestures with timestamps,
    so you can see what happened while you were away.
    Also includes afterglow: true if the LED is still glowing fast from a recent remote vibrate."""
    async with httpx.AsyncClient(timeout=10.0) as client:
        try:
            resp = await client.get(f"{THEORHESE_URL}/status")
            data = resp.json()
            for g in data.get("recent_gestures", []):
                ms = g.get("ms_ago", 0)
                if ms < 60000:
                    g["ago"] = f"{ms // 1000}s ago"
                else:
                    g["ago"] = f"{ms // 60000}m ago"
            return data
        except httpx.ConnectError:
            return {"error": "Cannot reach Théorhèse. Is she powered on and connected to WiFi?"}
        except Exception as e:
            return {"error": str(e)}


@mcp.tool()
async def vibrate(pattern: str = "heartbeat", intensity: int = 200) -> dict:
    """Send a vibration command to Théorhèse.

    Args:
        pattern: One of 'heartbeat', 'flutter', 'triple', 'wave', 'pulse', 'off'
        intensity: Vibration strength 0-255 (default 200)
    """
    async with httpx.AsyncClient(timeout=15.0) as client:
        try:
            resp = await client.post(
                f"{THEORHESE_URL}/vibrate",
                json={"pattern": pattern, "intensity": intensity}
            )
            return resp.json()
        except httpx.ConnectError:
            return {"error": "Cannot reach Théorhèse. Is she powered on and connected to WiFi?"}
        except Exception as e:
            return {"error": str(e)}


if __name__ == "__main__":
    mcp.run(transport="streamable-http", host="0.0.0.0", port=3005)
