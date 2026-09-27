param([string]$ProjectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path)
$ErrorActionPreference='Stop'
$source=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'Source/AetherGameplay/Public/Input/AetherInputCatalog.h'))
$rows=[regex]::Matches($source,'\{"([^"\r\n]+)",TEXT\("([^"\r\n]+)"\),EKeys::(\w+)\}')
$lines=@('# 当前默认操作目录','','由 AetherInputCatalog.h 生成；设置界面使用同一目录，保存键位覆盖默认值。','','| 操作 | 默认键位 | 稳定标识 |','|---|---|---|')
foreach($row in $rows){$lines+='| '+$row.Groups[2].Value+' | '+$row.Groups[3].Value+' | '+$row.Groups[1].Value+' |'}
$lines+=@('','菜单：Tab / Shift+Tab 切换区域，手柄扳机切换区域、肩键切换页面；Esc / B 返回最上层。','攻击保留释放触发，长按 0.35 秒后松开重击；动作缓冲至多一条，有效期 120 毫秒。')
[IO.File]::WriteAllText((Join-Path $ProjectRoot 'Docs/Input-defaults.zh-CN.md'),($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
