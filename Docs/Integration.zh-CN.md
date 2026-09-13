# UE 5.8 接入与运行

0.3 新增可玩修道院，运行 `Scripts/PlayAdventure.ps1`。剑盾、四法术、任务、存档与联网的详细入口见 [B–E 实施说明](Implementation-BCDE.zh-CN.md)。下文保留原材料实验场与插件接入方法。

## 运行独立实验场

工程入口：`C:\ueproject\test\AetherLab.uproject`。它使用引擎自带 Entry 地图，在运行时创建实验物体，不依赖下载的资产。

1. 用 UE 5.8 打开工程。如果提示缺少模块，先运行 `Scripts/Build.ps1`。
2. 点击 Play。场景、固定相机和 HUD 自动创建。
3. 选择 `1 热量 / 2 水 / 3 雷电 / 4 冷却 / 5 冲量`，再用鼠标点击带标签的样本。
4. `Tab` 同时启动火传播、积水导电、冻结和密闭燃料爆裂。
5. `R` 切换降雨，`G` 切换风，`Backspace` 重置实验场。

操作与输出数量使用 SI 单位，空间位置使用 UE 的厘米。HUD 中负热量是抽热；不是“冰属性伤害”。有些样本需要等待燃烧累计损伤后才坠落。

## 放到自己的 Actor

1. 将 `Plugins/ReactiveWorld` 整个目录复制到目标工程的 Plugins 下，启用 ReactiveWorld 和 GameplayAbilities，重新生成项目并编译。
2. C++ 游戏模块的 Build.cs 添加 `ReactiveRuntime`；直接使用纯数据类型时再添加 `ReactiveCore`。
3. 给有碰撞网格的 Actor 添加 `ReactiveBodyComponent`。
4. 选择 Wood/Metal/Water/Oil/Stone 预设，或创建 `ReactiveMaterialAsset` 数据资产覆盖完整材料参数。
5. 配置 `InteractionRadiusCm`，使交互球合理覆盖要模拟的那一部分；大物体按部件拆成多个反应节点。
6. 设置初始水量，不能超过该材料的容量。脚本会移动的物体启用 `bTrackMovement`。
7. 监听 `OnReaction` 处理 Gameplay 和自定义视觉；读取只读 `State` 展示当前状态。

材料资产在注册时被复制。运行过程中改数据资产不会自动热更新已注册的实体。若需要这种功能，应在同步点增加显式重新配置命令，处理焓、质量和容量迁移。

同一 Actor 当前使用找到的第一个 PrimitiveComponent 作为机械目标。复杂角色或多个网格应改为显式组件引用或按部件注册，不应依赖组件排列顺序。

## 输入接口

```cpp
#include "ReactiveBodyComponent.h"
#include "ReactiveWorldSubsystem.h"

FReactiveStimulus Heat;
Heat.HeatJ = 60000.0; // 此次输入总量 60 kJ
const bool Accepted = TargetBody->Inject(Heat);
```

范围输入把 `Target` 传空，提供位置与半径；总量在候选实体间均分：

```cpp
FReactiveStimulus Area;
Area.PositionCm = ImpactPoint;
Area.RadiusCm = 200.0;
Area.HeatJ = 60000.0;
GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->Submit(nullptr, Area);
```

`Accepted` 表示输入被排入队列，不代表效果已经发生。调用者必须处理 false。输入在后续固定步被求解；不能在 Inject 后立刻断言 State 已更新。队列满、NaN、负水量、无效句柄、越界参数或客户端注入会失败。

生产版范围命中还需验证施法原点与目标之间的遮挡、距离和权限。这个底层入口只接受可信的本地/服务器调用，不是可直接暴露给远端客户端的 RPC。

## 机械结果

- `bEnableChaosOnBreak`：完整度破坏后启用普通刚体；目标若是 GeometryCollection，则同时施加外部簇应变。
- 普通 Static Mesh 必须有简单碰撞。复杂碰撞作为简单碰撞的网格不能直接按普通动态刚体使用。
- `bIceControlsPawnCollision`：液态恢复初始碰撞设置，接近完全冻结且未破坏时阻挡 Pawn。
- 实验场坠落只验证调用路径；正式建筑需要支撑图、约束和制作好的 Geometry Collection。

## GAS 与 Niagara

插件发送的 Gameplay Event：`Event.Reactive.Shock`、`Event.Reactive.Ignited`、`Event.Reactive.Frozen`、`Event.Reactive.Broken`。Shock 的 EventMagnitude 是沉积电能（J），其他转换事件不要直接作为 HP 扣除量使用。自身没有 ASC 的普通道具也可以只监听 OnReaction。

若给 `BurningEffect` 指定 Niagara System，插件会在起火时激活它，并设置 `User.TemperatureC`、`User.FuelKg`、`User.Burning`。需在该 System 里创建同名用户参数。0.1 没有内置正式火焰/音效，也没有 NDC 资产；实验场使用颜色和调试标记。

## 自动检查

- `Scripts/Build.ps1`：编译 Editor 目标。
- `Scripts/Test.ps1`：运行 `Reactive.Core.*` 自动化测试，报告写入 `Saved/Automation/Reactive`。
- `Scripts/Smoke.ps1`：启动无渲染实验场，验证 GameMode、组件注册、步进、冻结、燃烧、电能与爆裂，结果写入 `Saved/Logs/AetherSmoke.log`。

详细执行结果见 `Docs/Verification.zh-CN.md`。无渲染测试能验证逻辑和桥接执行路径，不能代替可见图形效果和真实多人网络测试。
