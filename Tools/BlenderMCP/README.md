# Blender MCP 安装记录

2026-09-12 状态：按用户要求卸载后，用户改为使用 MCP 建模，已恢复同版本服务、Blender 插件与全局 `blender` 连接，并完成真实 MCP 往返验证。本次新成果在 `Art/SwordMagic`，见该目录的 `README.zh-CN.md`；旧 `Art/AetherLab` 保留。

已安装上游 [ahujasid/blender-mcp](https://github.com/ahujasid/blender-mcp) 1.9.1，复用 Steam 中的 Blender 5.2.1 LTS。Codex MCP 配置方式依据 [OpenAI 官方 MCP 文档](https://developers.openai.com/codex/mcp/)。

- Blender：`E:\steam\steamapps\common\Blender\blender.exe`
- MCP 服务：`C:\ueproject\test\Tools\BlenderMCP\.venv\Scripts\blender-mcp.exe`
- Python：项目内独立 venv，完整依赖版本见 `requirements-lock.txt`。
- Blender 插件：`%APPDATA%\Blender Foundation\Blender\5.2\scripts\addons\blender_mcp.py`，使用 wheel 自带的同版插件。
- Codex 全局 MCP 名称：`blender`，已通过 `codex mcp add` 注册。
- 连接：`127.0.0.1:9876`；插件随 Blender 启动。遥测环境开关与插件偏好均已关闭。

`install-report.json` 记录实际插件安装路径；`connection-report.json` 记录真实 MCP initialize、tools/list 和 tools/call 的结果。模型生成已通过 MCP 的 `execute_blender_code` 调度到 Blender 执行。

若当前 Codex 任务的工具列表没有自动刷新，重新打开任务或重启 Codex，以重新载入已保存的 MCP 配置。不要同时启动多个占用 9876 的 Blender 实例。

```powershell
codex mcp get blender
& 'C:\ueproject\test\Tools\BlenderMCP\.venv\Scripts\python.exe' 'C:\ueproject\test\Tools\BlenderMCP\check_mcp.py'
```

Blender 插件需要图形界面的事件循环，不能在 `--background` 模式接受 MCP 请求；批量建模和渲染脚本可以独立以 `--background` 运行。

重新安装插件时运行 `install_addon.py`，会保留不同旧版插件的 `.before-aetherlab.bak` 备份；写入 Blender/Codex 用户配置时需要相应的文件权限。
