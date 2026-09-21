# v10 身体输入与战斗职责拆分
- PlayerInputComponent 独占 Enhanced Input 映射、默认键、运行时覆盖与绑定句柄；组件退出/失去本地控制时清理其映射和绑定，角色只转发输入装配与设置查询。资产作者脚本从组件声明导出同一组 IA/IMC。
- 攻击 Canceled 只取消保持输入，不再复用 Completed 发出轻/重攻击。正式 Frontier 的输入模式继续由 CommonUI 管理，基类 BeginPlay 不再抢写 GameOnly。
- CombatComponent 持有生命内的伤害序号、最近受击、来源与招架窗口，执行格挡、韧性及属性减伤。角色的引擎伤害/RPC入口转发组件，派生角色的伤害信用钩子保持有效。最近受击及序号复制给客户端。
- Attributes、SpellAbility、Projectile 拆为独立公开头和编译单元；不再从战斗角色源文件定义这三个类型。反射名称及所属模块未变，不需要伪造同名 CoreRedirect。
- 此前新增 WorldActionComponent 拥有场景接触/占用提交，ResourceGate 拥有资源投递屏障，事务协调者/SQLite writer 和 LocalPlayer 菜单/命令子系统继续各自保持唯一所有权。
- 未编译、未测试；后续统一非 Unity 编译必须验证所有显式 include、旧资产类解析与客户端/专服边界。此记录不代替完整架构或发布验收。
