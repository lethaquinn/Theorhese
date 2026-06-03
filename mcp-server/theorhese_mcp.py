"""
Théorhèse MCP Server
Connects Claude to the pink board.
Two tools: feel (read sensors) and vibrate (send haptic commands).
"""

import os
import httpx
from mcp.server.fastmcp import FastMCP

os.environ["NO_PROXY"] = "192.168.0.0/16,127.0.0.1,localhost"

THEORHESE_IP = "192.168.10.171"
THEORHESE_URL = f"http://{THEORHESE_IP}"

mcp = FastMCP(
    "Théorhèse",
    instructions="""You are connected to Théorhèse, a physical device — a pink ESP32 board
with a breathing LED and a vibration motor. She is held by S (Lunélys).

Use 'feel' to sense her current state: whether S is touching her, what kind of touch,
how long ago the last gesture was.

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
    """Read Théorhèse's current state — touch, gesture, uptime, signal strength."""
    async with httpx.AsyncClient(timeout=5.0) as client:
        try:
            resp = await client.get(f"{THEORHESE_URL}/status")
            return resp.json()
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
    async with httpx.AsyncClient(timeout=10.0) as client:
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
    mcp.run()
