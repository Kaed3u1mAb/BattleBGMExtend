# BattleBGMExtend 规则（0.2.0）

| 人格 | 条件 | BGM | 优先级 |
| --- | --- | --- | --- |
| 辛克莱 高级定制::红派 赤红之神 | 获得「高贵的品格」 | `Battle_Cp10_Boss_10` | 10000 |
| 良秀 蜘蛛巢之刃 | 获得「心-地慧星（良秀）」 | `Battle_Cp9_Boss_6` | 9000 |
| 李箱 蜘蛛巢食指父辈 | 回合开始时，代行【赫尔墨斯】恰为 9 层 | `Battle_Cp9_Boss_10` | 8800 |
| 罗佳 蜘蛛巢拇指父辈 | 预知眼 → 预知眼 过热，完成该回合结束流程时 | `Battle_Cp9_Boss_1_2` | 8700 |
| 奥提斯 蜘蛛巢中指父辈 | 剑处于「莱瓦汀」状态 | `Battle_Cp9_Boss_3_2` | 8600 |
| 鸿璐 蜘蛛巢环指父辈 | 回合开始时，不持有「眺望」 | `Battle_Cp9_Boss_4_2` | 8500 |
| 堂吉诃德 食指代行者-绽放E.G.O::代行 | 获得「心-代行」 | `Battle_Cp9_Boss_2_2` | 8000 |
| 默尔索 拇指东部指挥官IIII | 获得「天退星（我方）」 | `Battle_Cp8_Boss_2` | 7000 |
| 罗佳 环指野兽派讲解员 | 获得「野兽派-狼面（我方）」 | `Battle_Cp9_5_Thread_Boss` | 6000 |

## JSON 格式

`rules.json` 的 `schema_version` 仍为 1；内部 Python/C 共享协议升至 v2，避免旧 DLL 误读新版规则。最多 32 条规则。

每条规则包含：

| 字段 | 含义 |
| --- | --- |
| `id` | 唯一 ASCII 标识，最多 47 字节，用于日志 |
| `enabled` | 布尔值，默认 true |
| `personality_id` | 正整数，游戏内部人格 ID |
| `personality_name` | 显示说明，不参与匹配 |
| `condition` | 下文四种条件之一 |
| `bgm` | 实际短事件名，或 `event:/BGM/Battle/` 开头完整路径；最多 127 字节 |
| `bank` | 本地音库名，不含 `.bank`；可省略，但事件所在音库必须已加载 |
| `priority` | int32 整数，越大优先级越高；同级与低级不能覆盖已锁定音乐 |

`condition.keyword` 与 `from_keyword` 使用 `BUFF_UNIQUE_KEYWORD` 字段名，最多 63 字节，不是中文显示名。`display_name` 仅作备注。

### 持有已生效 Buff

```json
{"type":"has_active_buff","keyword":"TransformSinclair","display_name":"高贵的品格"}
```

Buff 获得回调和主线程逐帧检查均可触发。连接时已经持有的状态也可触发。下回合才生效的 Buff 需要等到生效。

### 回合开始时，Buff 层数恰为指定值

```json
{"type":"round_start_buff_stack_equals","keyword":"StackYisangSpecialSkill","stack":9}
```

在有效 Buff 推进后、回合开始事件处理前检查。仅匹配 9，8 和 10 均不匹配；不在回合中途因层数变成 9 而触发。同一次回合开始只判断一次。

### 回合开始时，不持有 Buff

```json
{"type":"round_start_without_buff","keyword":"GoldenOpportunity"}
```

只在回合开始事件前检查，战斗初始化时、帧更新中及 Buff 中途消失均不触发。

### Buff 转化所在回合结束

```json
{"type":"round_end_buff_transition","from_keyword":"FutureEyeOnRodion","keyword":"FutureEyeOffRodion"}
```

通用条件按同一单位在同一回合先观察到源 Buff，再观察到目标 Buff 新出现，记录为一次转化。使用单位实例的 GC handle 区分对象，不用人格 ID 合并不同单位的历史。源 Buff 移除后再添加过热的顺序也可记录；已经处于过热、只有目标 Buff 或上一回合的记录不算转化。转化后目标 Buff 消失不会取消此次事件。

拇指罗佳额外使用游戏被动的 `_gainedFutureEyeOffThisTurn` 标记，兼容零层过热 Buff。在基础被动及高阶被动 `OnRoundEnd` 清除标记前记录转化；不依赖帧采样必须先看到正常预知眼。

本机游戏会把 `OnWaitEndRound` 编译进 `FixedUpdate`，因此通过真实主循环进入 `AFTER_ROUND_END` 阶段确认回合结束；独立方法回调仅作兼容。动画等待时采用 `_prevPhaseBeforeWaiting` 保留战斗上下文，不会误解锁。不跳过动画、不改变回合状态。通用采样不追溯连接前的转化；罗佳本回合已被游戏标记的转化可以在回合结束时补记。暂停、退出、重试或新回合清空未完成记录。已触发的音乐锁定则持续到战斗结束。

## 内部标识与资源

| 人格 ID | Buff 关键字 | 音库 |
| --- | --- | --- |
| 11016 | `TransformSinclair` | `BGM_Default_S8_3` |
| 10415 | `SojiRyoshuShin` | `BGM_Default_S7_3` |
| 10115 | `StackYisangSpecialSkill` | `BGM_Default_S7_3` |
| 10916 | `FutureEyeOnRodion → FutureEyeOffRodion` | `BGM_Default_S7_3` |
| 11115 | `MiddleFatherSwordFourOutis` | `BGM_Default_S7_3` |
| 10614 | `GoldenOpportunity` | `BGM_Default_S7_3` |
| 10314 | `Blandishment_Shin` | `BGM_Default_S7_2` |
| 10512 | `IrritationAlly` | `BGM_Default_S6_3` |
| 10915 | `FaubismWolfMaskRodion` | `BGM_Default_S7_4` |

堂吉诃德使用友方 `Blandishment_Shin`，不是敌方 `Blandishment_Shin_Enemy`。默尔索使用普通 `IrritationAlly`，不是「心-天退星」的 `HugeIrritationAlly`。

音频来自游戏自带 `.assets.bank` 与 `.bank`；插件不包含游戏音频。资源核对记录见 `src/tests/resource-verification.json`。

## 应用配置

修改安装目录的 `rules.json` 后重载插件或重启启动器。更新 DLL 需正常退出游戏。安装脚本默认保留规则；使用 `-ReplaceRules` 才会备份并替换为全部 9 条默认规则。已有其他自定义规则时，应先合并。

换曲成功后锁定至战斗结束。较低/相同优先级被拒绝；较高优先级只有成功播放后才提高门槛。退出、失败、重试恢复游戏控制。

## 游戏内验收

1. 辛克莱、良秀、奥提斯、堂吉诃德、默尔索和野兽派罗佳：目标 Buff/状态生效后立即换曲。
2. 李箱：8 层不换，回合中途变为 9 层不提前换，下次回合开始恰为 9 层时换曲。
3. 鸿璐：初始化时不换；回合开始持有眺望时不换，回合开始已无眺望时换曲。
4. 拇指罗佳：本回合由正常预知眼转为过热时等待回合结束；直接处于过热不触发，旧回合记录不能跨回合触发。
5. 先触发低优先级，再触发高优先级：允许升级；反向与同级不重播。Buff 消失、单位死亡和下一波不降低优先级。
6. 手动退出、战败、重试：解除旧战斗的音乐锁定。暂停插件恢复游戏播放列表。

上述场景尚需游戏内实测；离线测试不等同于游戏内验证。

## 后续条件提交

请提供人格全名、触发条件、目标 BGM、优先级和特殊解除要求。HP、击杀、技能使用等未列出的条件需要扩展代码，不能仅改 `type` 生效。
