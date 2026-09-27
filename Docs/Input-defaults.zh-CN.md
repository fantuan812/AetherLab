# 当前默认操作目录

由 AetherInputCatalog.h 生成；设置界面使用同一目录，保存键位覆盖默认值。

| 操作 | 默认键位 | 稳定标识 |
|---|---|---|
| 攻击：松开轻击／蓄力重击 | LeftMouseButton | Attack |
| 格挡 | RightMouseButton | Guard |
| 冲刺 | LeftShift | Sprint |
| 跳跃／翻越 | SpaceBar | Jump |
| 下蹲 | LeftControl | Crouch |
| 物件操作修饰键（LB） | Gamepad_LeftShoulder | UtilityModifier |
| 切换蹲姿（LB+B） | Gamepad_FaceButton_Right | CrouchToggle |
| 闪避 | LeftAlt | Dodge |
| 施放所选技能 | MiddleMouseButton | Cast |
| 选择技能一 | One | One |
| 选择技能二 | Two | Two |
| 选择技能三 | Three | Three |
| 选择技能四 | Four | Four |
| 交互 | E | Interact |
| 使用生命药剂 | Q | Potion |
| 使用法力药剂 | Z | Z |
| 搬起／放下 | G | Carry |
| 推动 | V | Push |
| 投掷搬运物件 | C | Throw |
| 锁定目标 | F | Lock |
| 装备所选物品 | R | R |
| 切换盾牌 | T | T |
| 拆分堆叠 | B | B |
| 合并堆叠 | N | N |
| 菜单内切换区域 | Tab | Tab |
| 背包出售／队伍解散同伴 | Delete | Delete |
| 购买法力药剂 | Seven | Seven |
| 购买口粮 | Eight | Eight |
| 背包 | I | I |
| 任务日志 | J | J |
| 技能 | K | K |
| 地图 | M | M |
| 队伍 | P | P |
| 系统菜单／返回 | Escape | Escape |
| 领取任务奖励 | Enter | Claim |
| 同伴跟随／留守 | H | H |
| 邀请组队 | Y | Invite |
| 接受邀请 | U | AcceptInvite |
| 离开队伍 | O | LeaveParty |
| 回据点恢复 | F8 | F8 |
| 请求保存 | F5 | F5 |
| 诊断信息 | F10 | Debug |
| 开发天气控制 | F11 | Weather |

菜单：Tab / Shift+Tab 切换区域，手柄扳机切换区域、肩键切换页面；Esc / B 返回最上层。
攻击保留释放触发，长按 0.35 秒后松开重击；动作缓冲至多一条，有效期 120 毫秒。

手柄游戏状态：LB 仅作修饰键；LB+↑ 搬起/放下、LB+→ 投掷、LB+↓ 推动、LB+← 法力药；LB+B 切换蹲姿，单独 B 闪避。
倒地交互层：方向键选择等待救援或回据点，A 确认；Y 直接请求回据点。键盘恢复键跟随 F8 动作的自定义绑定。
键盘 Crouch 的自定义键保持不变；新增 UtilityModifier/CrouchToggle 为独立手柄语义，不覆盖玩家键盘重绑。
