"""Run once in Blender to install the pinned, bundled MCP add-on."""
from pathlib import Path
import bpy
import json

ROOT = Path(__file__).resolve().parents[2]
source = ROOT / 'Tools/BlenderMCP/.venv/Lib/site-packages/blender_mcp/bundled/addon.py'
# Install the exact add-on shipped with blender-mcp 1.9.1.
addon_dir = Path(bpy.utils.user_resource('SCRIPTS', path='addons', create=True))
destination = addon_dir / 'blender_mcp.py'
if destination.exists() and destination.read_bytes() != source.read_bytes():
    backup = destination.with_suffix('.py.before-aetherlab.bak')
    if not backup.exists():
        backup.write_bytes(destination.read_bytes())
destination.write_bytes(source.read_bytes())
bpy.utils.refresh_script_paths()
bpy.ops.preferences.addon_enable(module='blender_mcp')
prefs = bpy.context.preferences.addons['blender_mcp'].preferences
prefs.telemetry_consent = False
bpy.context.scene.blendermcp_auto_start_server = True
bpy.ops.wm.save_userpref()
report = {'blender': bpy.app.version_string, 'addon': str(destination),
          'enabled': 'blender_mcp' in bpy.context.preferences.addons,
          'telemetry': prefs.telemetry_consent}
(ROOT / 'Tools/BlenderMCP/install-report.json').write_text(json.dumps(report, indent=2))
print('AETHER_MCP_INSTALL', json.dumps(report))
