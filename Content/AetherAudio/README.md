# 反应音频生成目录

唯一作者输入：`ContentSource/Audio/catalog.json` 与其带 SHA-256 和来源的 WAV。
生成目录 `Generated/g_<source-digest-prefix>_<unique-run-id>/` 不可变，不覆盖上次发布的 SoundWave 或目录。

编辑器作者流程分两次独立进程：build 保存新目录；verify-publish 冷读并比较全部参数/变体顺序、检查 AssetManager 注册后，
才以单次文件替换更新 DefaultGame.ini 中 CatalogId 与对应 Cook rule。发布后需重新启动 UE。
失败时保留上次已发布目录及原文件，新源没有生效；中断的未发布目录保留，不自动删除。
显式禁用事件应设置 unavailable_reason 且 variants=[]，作为新目录的不可播放定义发布。

当前未运行这两个进程，也没有生成 `.uasset`。机制与代码不代表实际导入、冷读、Cook 或播放已验收。

不同 PID 不证明 build 已退出，须先完全退出 build 进程。脚本发布锁不约束外部编辑器/人工写 INI，操作期间避免并改。
