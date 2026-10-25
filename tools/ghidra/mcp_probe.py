"""End-to-end check of the GhidraMCP stdio bridge against the live Ghidra instance.

Run with the ghidra-mcp venv interpreter (system Python has no `mcp` module):

    C:\\Tools\\DarkSoulsRemaster_De\\ghidra-mcp\\.venv\\Scripts\\python.exe ^
        tools\\ghidra\\mcp_probe.py [toolName] [json-args]

With no arguments it performs the MCP handshake, reports how many tools the
bridge advertises, lists the connected Ghidra instances and reads program
metadata. With a tool name it calls that tool and prints the text result, which
is how a static probe runs without any hand-rolled HTTP or headless Ghidra.
"""

import asyncio
import json
import os
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

BRIDGE = os.environ.get(
    "BRIDGE_MCP_GHIDRA",
    r"C:\Tools\DarkSoulsRemaster_De\ghidra-mcp\.venv\Scripts\bridge-mcp-ghidra.exe",
)


async def run(tool: str | None, args: dict) -> int:
    params = StdioServerParameters(command=BRIDGE, args=os.environ.get("MCP_PROBE_ARGS", "").split())
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            init = await session.initialize()
            print(f"server={init.serverInfo.name} {init.serverInfo.version}")

            listed = await session.list_tools()
            names = sorted(t.name for t in listed.tools)
            print(f"tools={len(names)}")

            instances = await session.call_tool("list_instances", {})
            print("instances:", instances.content[0].text.strip())

            if tool is None:
                meta = await session.call_tool("get_metadata", {})
                print("metadata:", meta.content[0].text.strip()[:400])
                print("tools:", ", ".join(names))
                return 0

            result = await session.call_tool(tool, args)
            if result.isError:
                print(f"ERROR {tool}:", result.content[0].text.strip())
                return 1
            print(f"{tool}:", result.content[0].text.strip()[:2000])
            return 0


if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else None
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    raise SystemExit(asyncio.run(run(name, payload)))
