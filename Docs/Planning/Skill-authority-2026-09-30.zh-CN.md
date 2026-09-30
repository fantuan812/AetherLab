# 玩家账本与 NPC 能力定义分离

接续原生启动批。本批仅收口技能授权，不代表旧任务/世界投影已完成拆除。

## 当前契约

- 服务器生成角色前确定 SkillAuthority：Profile 玩家只从当前持久账本和已授权来源得到技能；Definition NPC只从 NpcSkills.json 当前定义得到初始GAS能力。不是以“档案暂时没加载”决定回退模式
- 玩家角色构造时即为 Profile，未就绪时只授予近战/移动等公共动作，绝不先借用NPC法术再撤销。NPC定义授权API拒绝Profile角色及持有玩家PlayerState的角色
- SpawnFighter 在 FinishSpawning 前从数据解析 Fighter→Loadout；缺失定义拒绝生成。招募动作通过 Interactions.json ServiceId 指定同行者定义，治疗同行者保留真实 Water.Draw GAS能力
- 基类法术身份/输入读取真实GAS身份标签与InputID，不再经 LegacyBit 或数值等级推断身份。既有同一NPC初始授权重试保留有效SpecHandle和服务器已调整的rank，不创建重复能力
- NpcSkills.json 只包含初始技能rank/slot，不能包含玩家任务、库存、交易或存档授权。依赖当前 Skills 定义交叉校验，未知技能/rank、重复槽和字段、未知版本一律拒绝
- 读取后本进程不可变，修改后需要重新启动。沿用 Definitions NonUFS staging，无资源路径或缺文件兜底

## 验证边界

新增真实生产 SpawnFighter→GrantSpells→GAS 绑定测试，覆盖玩家未就绪、NPC治疗技能、玩家不能借NPC配置授权、缺NPC定义拒绝生成；保留并调整原有rank执行/水守恒/重入消耗测试的显式NPC夹具，不删仍受支持的断言。

未编译、未测试、未运行客户端/服务器或同行者实玩。当前仍有旧 Profile/World 的只读投影以及历史库存/交互实现，后续必须逐域替换；不得以本批角色分类替换就声称所有兼容层已删除。

## 独立审查闭环

仍受支持的 Adventure/艺术场景/设备与截图探针改为显式数据化角色生成，在 BeginPlay 之前绑定同一NpcSkills定义，不恢复旧数字法术表。

遭遇波次及营地使用同一个生产批量生成入口：任一角色失败清理本批已创建对象；波次进入Failed，营地进入bSpawnFailed，不持有可结算实例、不标清场、不给击杀/任务信用。营地离开现有卸载距离后可重新尝试。所有现存SpawnFighter直接调用补空指针检查。

新增失败路径使用生产SpawnCamp→SpawnFighterBatch→SpawnFighter：第一名成功、第二名未知类型时验证部分实体清理、不可领奖、无实例和无信用。测试在隔离world中手动初始化ASC/调用GrantSpells；这不证明BeginPlay自动授权、真实AI支持施法或网络复制，以上仍未运行。

Adventure装配失败还在Interact/SaveAdventure/LoadAdventure入口统一拒绝，避免F5把半场景覆盖原探针存档；新增唯一隔离SaveSlot回归，断言失败时不创建存档（未运行）。

数据作者注意：FighterLoadouts.Player 中的 Player 是既有战斗/探针的形体类别，映射 TrainingElementalist 只供明确选择 Definition 权限的训练或 Adventure 探针角色。它不授予真实 Profile 玩家任何技能；Frontier玩家构造即为Profile，NPC授权入口还明确拒绝持有玩家PlayerState的Actor。不要把这个类别名当成账号或进度授权。
