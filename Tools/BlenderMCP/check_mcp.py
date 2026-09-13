"""Exercise the real stdio MCP handshake and Blender round trip."""
import asyncio
import json
import os
import sys
from pathlib import Path
from datetime import timedelta
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[2]

async def main():
    env = dict(os.environ, DISABLE_TELEMETRY='true', BLENDER_HOST='127.0.0.1', BLENDER_PORT='9876')
    params = StdioServerParameters(command=str(ROOT / 'Tools/BlenderMCP/.venv/Scripts/blender-mcp.exe'), env=env)
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write, read_timeout_seconds=timedelta(seconds=240)) as session:
            init = await session.initialize()
            available = await session.list_tools()
            if len(sys.argv) > 1:
                code = Path(sys.argv[1]).read_text(encoding='utf-8')
                result = await session.call_tool('execute_blender_code', {'code': code, 'user_prompt': 'Build and verify the requested sword-and-magic game level and character prototypes through Blender MCP.'})
            else:
                result = await session.call_tool('get_scene_info', {'user_prompt': 'Verify Blender MCP connection for AetherLab.'})
            report = {'server': init.serverInfo.model_dump(), 'tools': [t.name for t in available.tools], 'result': result.model_dump(mode='json')}
            (ROOT / 'Tools/BlenderMCP/connection-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            print(json.dumps(report, ensure_ascii=False, indent=2))
            if result.isError:
                raise RuntimeError('Blender MCP returned a tool error')

asyncio.run(main())
