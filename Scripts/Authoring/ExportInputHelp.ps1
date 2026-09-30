param([string]$ProjectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path)
$ErrorActionPreference='Stop'
$source=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'Source/AetherGameplay/Public/Input/AetherInputCatalog.h'))
$rows=[regex]::Matches($source,'\{"([^"\r\n]+)",TEXT\("([^"\r\n]+)"\),EKeys::(\w+)\}')
$lines=@('# 当前默认操作目录','','由 AetherInputCatalog.h 生成；设置界面使用同一目录，保存键位覆盖默认值。','','| 操作 | 默认键位 | 稳定标识 |','|---|---|---|')
foreach($row in $rows){$lines+='| '+$row.Groups[2].Value+' | '+$row.Groups[3].Value+' | '+$row.Groups[1].Value+' |'}
$actions=Get-Content -LiteralPath (Join-Path $ProjectRoot 'Content/AetherCore/Definitions/V10/Actions.json') -Raw | ConvertFrom-Json
if($actions.SchemaVersion -ne 1){throw 'Unsupported action schema'}
$charge=$actions.Input.AttackCharge
$bufferMs=$actions.Input.AttackBuffer*1000
$lines+=@('','菜单：Tab / Shift+Tab 切换区域，手柄扳机切换区域、肩键切换页面；Esc / B 返回最上层。',"攻击保留释放触发，长按 $charge 秒后松开重击；动作缓冲至多一条，有效期 $bufferMs 毫秒。")
$lines+=@('','手柄游戏状态：LB 仅作修饰键；LB+↑ 搬起/放下、LB+→ 投掷、LB+↓ 推动、LB+← 法力药；LB+B 切换蹲姿，单独 B 闪避。','倒地交互层：方向键选择等待救援或回据点，A 确认；Y 直接请求回据点。键盘恢复键跟随 F8 动作的自定义绑定。','键盘 Crouch 的自定义键保持不变；新增 UtilityModifier/CrouchToggle 为独立手柄语义，不覆盖玩家键盘重绑。')
[IO.File]::WriteAllText((Join-Path $ProjectRoot 'Docs/Input-defaults.zh-CN.md'),($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
