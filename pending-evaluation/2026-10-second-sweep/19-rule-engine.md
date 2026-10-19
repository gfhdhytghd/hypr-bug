# 19 — 窗口/图层规则引擎审计（matchEngine / Rule / Applicator / recheck / reserved）

基线：main @ e38076ac9（fork）。域：`src/desktop/rule/**`、`src/desktop/reserved/`、COverridableVar 优先级覆盖。
查重声明：F-RULE-01（RE2 未验证即参与匹配、negative: 反转放行）、F-RULE-02/03（exec_token 共享秘密、/proc 同步读）已另行报告，本卷不重复；Lua 规则 API 的参数校验归 config-lua 域。
本卷所有匹配面均假定攻击者为任意 Wayland 客户端（class/title/role/xdg-tag/layer namespace/pid 等 metadata 全可控）。

---

## F-MATCH-01 ｜CLayerRule::matches 对不支持的 match 属性静默跳过 → 图层规则 fail-open，可绕过锁屏输入/遮挡 — 严重程度：高

- **位置**：
  - `src/desktop/rule/layerRule/LayerRule.cpp:100-115`（`matches()` 的 `switch` 只有 `case RULE_PROP_NAMESPACE`，其余走 `default:` 仅 LOG(TRACE) 后**当作匹配通过**）
  - `src/desktop/rule/Rule.cpp:90-106`（`registerMatch` 无规则类型白名单：layer 规则可注册 class/title/tag/float/workspace 等任意 `eRuleProperty`，全部计入 `m_mask`）
  - 注册入口共享：`src/config/lua/bindings/LuaBindingsConfigRules.cpp:1344-1347`（`hl.layer_rule` 的 match 表接受 `matchPropFromString` 能解析的**任意**键）
  - 效果侧：`LayerRuleApplicator.cpp:103-116`（order / above_lock / ignore_alpha 均以 PRIORITY_WINDOW_RULE 落盘）
  - 消费侧：`src/render/Renderer.cpp:991-992`（锁屏期间 `aboveLock != 0` 的层被绘制）、`src/desktop/state/ViewHitTester.cpp:342-344`（锁屏期间 `aboveLock == 2` 的层**接受输入**）、`parseAboveLock` 允许 0..2（`LayerRule.cpp:44-50`）
- **描述**：layer 规则唯一有实现效果的匹配属性是 `namespace`，但引擎在注册期允许任意属性、在匹配期对未实现属性“跳过即通过”。同时 `canMatch()` 只要求 `m_matchEngines` 非空。两个后果：
  1. **fail-open**：形如 `hl.layer_rule{ match={title="x"}, above_lock=2 }` 的规则对**所有** layer surface 生效（title 引擎被 default 分支吞掉，namespace 未注册则无任何约束）。
  2. **namespace 冒充**：即使配置者只写 namespace 匹配——wlr-layer-shell 在 Hyprland 中对所有客户端开放，namespace 是纯客户端字符串——任何进程都能 `get_layer_surface` 后 `set_namespace("bar-privileged")` 冒充用户为可信面板（hyprlock 周边、状态栏等）写的规则，直接继承 `above_lock=2`。
- **攻击路径**：恶意进程 → 创建 layer-shell surface，namespace 冒充可信组件 → map 时 `LayerSurface.cpp:182 propertiesChanged(RULE_PROP_ALL)` → 命中用户为可信组件写的 `above_lock` / `order` / `ignore_alpha` 规则 → 会话被 lock 后，`Renderer.cpp:991` 仍渲染该层，`ViewHitTester.cpp:344` 还允许其接收键鼠输入 → 锁屏上的点击劫持/输入覆盖（配合全屏不透明层即锁屏绕过 UI）。
- **文字 PoC**：用户配置 `layerrule above_lock 2, ^mynotifier$`（本意让通知中心盖过锁屏）。攻击者程序 bind `zwlr_layer_shell_v1`，`get_layer_surface(..., namespace="mynotifier")`，铺一个全屏不透明 surface 并捕获输入。锁屏（hyprlock）后攻击 surface 位于锁屏之上且是输入目标——用户解锁点击全部落在攻击者窗口上。
- **修复**：① `registerMatch`/规则构建期按 `eRuleType` 白名单校验（layer 仅允许 `RULE_PROP_NAMESPACE`，window 拒绝 `NAMESPACE`），越界属性直接报错拒绝注册；② `matches()` 的 `default:` 分支改为 fail-closed（未识别属性 ⇒ 返回 false），与 `IMatchEngine` 基类默认拒绝的哲学一致；③ 对 `above_lock >= 1` 的效果施加额外门禁（仅当 namespace 命中白名单为 compositor 校验过的可信 client，或该 surface 属于 lock 协议持有者）。

---

## F-MATCH-02 ｜applyDynamicRule 用拷贝赋值整体替换 COverridableVar → 抹掉更高优先级（SET_PROP/LAYOUT/WORKSPACE_RULE）覆盖，客户端可反复触发 — 严重程度：中高

- **位置**：
  - `src/desktop/rule/windowRule/WindowRuleApplicator.cpp:144-157`（active/inactive border_color）、`:171-173`（alpha/alphaInactive/alphaFullscreen）、`:211`（maxSize）、`:241`（minSize）——全部形如 `m_x.first = Types::COverridableVar(v, PRIORITY_WINDOW_RULE);`
  - `src/desktop/types/OverridableVar.hpp:52-64`（`operator=` 遍历全部 `PRIORITY_END` 槽位整体覆写：源对象为空的槽被写成 `nullopt`，**包括比 WINDOW_RULE 更高的 PRIORITY_SET_PROP 与更低的 LAYOUT/WORKSPACE_RULE**）
  - 对比正确用法：同文件其余 bool/int 属性均用 `m_x.first.set(v, PRIORITY_WINDOW_RULE)`（只动自己的槽）
  - 受害者：`src/config/shared/actions/ConfigActions.cpp:826, 835, 857-868`（`setprop maxsize/minsize/windowopacity/bordercol*` 写入 `PRIORITY_SET_PROP`）
  - 客户端可控触发器：`Window.cpp:936`（`set_title`/`set_class` 提交 → `propertiesChanged(TITLE|CLASS)`）、`FocusState.cpp:138/197/209`（focus → `RULE_PROP_FOCUS`）、`FullscreenHandler.cpp:178-183`、`XDGDialog.cpp:38`、`XDGTag.cpp:22`
- **描述**：只要某条规则的 match 掩码命中本轮 `props`，`applyDynamicRule` 就被再次执行；opacity/border/maxsize/minsize 四类属性走“整体赋值”而非 `set()`，等价于把该窗口这些属性的**全部优先级层**重置为“仅规则优先级”，用户/上游经 `setprop` 施加的 PRIORITY_SET_PROP 值被静默清空，且 `unset(PRIORITY_WINDOW_RULE)` 也永远无法恢复。
- **攻击路径**：管理员/用户经 `hyprctl dispatch setprop windowopacity 1,<该窗口>` 强制某可疑窗口不透明（截图取证、防半透明钓鱼），或经 `setprop maxsize` 限制其尺寸；攻击者进程在 xdg_toplevel 上反复 `set_title("a")/set_title("b")`（合法协议、纯客户端动作）→ 每次提交触发 `propertiesChanged(TITLE|CLASS)` → 任何对该窗口生效的 opacity/maxsize 规则重新执行整体赋值 → SET_PROP 覆盖被抹掉，取证截图得到隐形窗口 / 尺寸限制失效。
- **文字 PoC**：配置含 `windowrulev2 opacity 0, ^phishing$`。执行 `setprop windowopacity 1,phishing-win` 后用 `getprop` 验证为 1；然后对窗口 `set_title` 一次，再 `getprop` → 回到 0（规则值），SET_PROP 槽已空。
- **修复**：五处赋值改为 `set(value, Types::PRIORITY_WINDOW_RULE)`（border_color 需要清旧值时先 `unset(PRIORITY_WINDOW_RULE)` 再 `set`）；或给 COverridableVar 增加 `overwrite(prioritySlot)` 语义，禁止对整体 `operator=` 的使用（可从 API 上删除公有赋值）。

---

## F-MATCH-03 ｜同轮 propertiesChanged 内的 metadata→tag→规则级联（顺序依赖），tag 无命名校验 — 恶意标签注入链 — 严重程度：中

- **位置**：
  - `WindowRuleApplicator.cpp:179-188`（tag 效应：`m_dynamicTags.emplace_back(tag, mask)` + `m_tagKeeper.applyTag(effect, true)`，effect 字符串原样入库，无字符/前缀校验）
  - `WindowRuleApplicator.cpp:592-609`（`propertiesChanged` 单趟按注册序遍历规则：A 规则加的 tag 对**同趟稍后**的 B 规则立即生效；B 在前则漏到下一轮 → 行为依赖规则书写顺序）
  - `WindowRule.cpp:414-416`（tag 匹配直接读活的 `m_tagKeeper`）
  - `WindowRuleApplicator.cpp:549-551`、`564-583`（`propsToRecheck |= RULE_PROP_TAG` → `recheckStaticRules` 把 tag 命中转化为**静态特权**：workspace/monitor/fullscreen/pin/group）+ `Window.cpp:1342-1346`
  - `WindowRuleApplicator.cpp:68-79`（resetProps：两条规则加同名动态 tag 时，任一规则被重置即 `removeDynamicTag` 抹掉共享 tag——特权忽隐忽现）
- **描述**：tag 是规则系统的“能力令牌”，但签发只由客户端可控元数据决定：客户端设 `title=PHISH` → 命中 A 规则（`tag:+report`）→ 同趟 B 规则（`matches,tag,report` → `nofocus`/`pin`/`allows_input`/`workspace 2`）立即生效；静态特权则经 recheck 落地。引擎无“tag 必须由不可被客户端元数据触发的规则签发”的分层概念，也限制不了级联深度；tag 字符串未过滤，允许写入 `negative:…`、`*` 结尾等与保留语法冲突的名字。另外双规则共享 tag 时 reset 即整体删除，属同一族的语义缺陷。
- **攻击路径**：受害者配置了“某 tag 触发脚本/联动”（tagtoggle + exec 监听，或 tag 条件的窗口特权规则）。攻击者让自家窗口标题恰含触发 A 规则的片段，即可自选是否戴上该 tag，进而命中 tag 条件规则获得 nofocus/pin（反调试/防抓取）、把窗口投送到受害者工作区（钓鱼）或规避 `nofocus` 类限制。
- **文字 PoC**：配置：`windowrulev2 tag:+trust,title:^Firefox$`；`windowrulev2 noscreenshare,tag:trust`、`windowrulev2 workspace 2,tag:trust`。攻击窗口 `set_title("Firefox")` 于首次 commit 前（`commitWindow(initialCommit)` → `readStaticRules(true)` 在 map 前就以客户端 title 匹配）→ 获得 `trust*` tag → 同趟 `noscreenshare` 生效（屏幕共享取证中隐身）→ recheck 后 `workspace 2` 把钓鱼窗口送进受害者工作区。
- **修复**：① tag 匹配/签发分轮收敛：`propertiesChanged` 单趟内新加的 tag 不参与本趟匹配（统一等 recheck），消除顺序依赖；② 对 `applyTag`/规则 tag 串做词法校验（禁止 `negative:` 前缀、`*`、空串，限字符集与数量）；③ `m_dynamicTags` 改引用计数，全部持有者失效后才 `removeDynamicTag`。

---

## F-MATCH-04 ｜CIntMatchEngine 解析失败静默降级为“匹配 0”/前缀宽松解析 — 针对客户端可控 int 属性的误匹配 — 严重程度：低

- **位置**：`src/desktop/rule/matchEngine/IntMatchEngine.cpp:6-14`；消费点 `WindowRule.cpp:443-451`（fullscreenstate internal/client）、`:476-478`（exec_pid fallback）
- **描述**：`std::stoi` 三宗罪：① 失败走 `catch(...)` 只打日志，`m_value` 保持初值 **0** —— 规则值写错/超 int 范围（`out_of_range`）后语义变成“匹配 fullscreen_state==0”，从“笔误规则”变成“匹配所有无状态窗口”的 fail-open；② 接受前缀（`"12junk"`→12），与其余引擎的全匹配哲学不一致；③ int64 pid 超出 int 范围时同样塌缩为 0，exec 规则 pid 校验（F-RULE-02 域）叠加此缺陷后可能误匹配 `_NET_WM_PID` 可被 X11 客户端任意伪装的 pid==0 判定路径。
- **攻击路径**：配置作者写出 `matches,fullscreenstate_client,3x` 这类值 → 静默变成匹配 client==0；攻击者只需不请求 fullscreen 即命中该本意“只在 fullscreen 时生效”的规则（可承载 float/pin/nofocus 等效应）。
- **文字 PoC**：`windowrulev2 noscreenshare,fullscreenstate,0 2x` —— client 侧本意匹配 `"2x"` 永不生效（安全默认），实际匹配 client==0，任意普通窗口自动 no_screen_share；反之作者本意匹配 0 的约束可被误配成别的值。
- **修复**：解析失败 ⇒ 该规则标记无效（不匹配任何东西）并打 ERROR；用 `strToNumber` 全匹配语义（与 effect 解析统一）；int 引擎字段用 int64。

---

## F-MATCH-05 ｜CTagKeeper::isTagged 负向语义 off-by-one：`starts_with("negative")` + `substr(9)` — 取反语义错误 — 严重程度：低

- **位置**：`src/helpers/TagKeeper.cpp:5-10`；入口 `TagMatchEngine.cpp:11-13`；tag 存储 `TagKeeper.cpp:12-41`
- **描述**：负向匹配以 `starts_with("negative")`（8 字符）判定却 `substr(9)`（按含冒号 9 字符切）。① 名为 `negativeXxx` 的真实 tag 在规则里会被切成 `Xxx` 去掉 1 个字符的残体做**取反**匹配，语义完全错位；② 规则串恰为 `negative`（≤9 字符）⇒ 目标为空串 ⇒ 取反恒真：**无条件匹配所有窗口**（fail-open）；③ 规则串为空 ⇒ `isTagged("")` 因非严格模式检查 `"" + "*"`，任何带动态 tag（都以 `*` 存储）的窗口全部命中；④ `applyTag("")` 会把字面 `"*"` 塞进集合，污染后续所有非严格查询。负向匹配只存在于 tag/regex 两引擎，bool/int 无取反——文档/代码中 `noclass`/`notag` 直觉写法实际不支持，配错也不报错。
- **攻击路径**：配置作者按直觉写 `matches,tag,negative`（想要“无该 tag 反向放行”）⇒ 规则恒真，绑定的 pin/nofocus/workspace 特权落到全部窗口。
- **文字 PoC**：`windowrulev2 noscreenshare,tag,negative` → 所有窗口（含攻击者流媒体窗口）自动 no_screen_share；`matches,tag,negativetagA` 实际判断的是 tag `"agA"` 的缺失。
- **修复**：只识别显式 `"negative:"` 前缀（长度一致），前缀外字符即普通 tag；空 tag/`*` 拒绝入集与查询；未支持的 `notag/noclass` 在配置解析期报错。

---

## F-MATCH-06 ｜max_size/min_size/size 表达式结果无非有限校验：inf/NaN 直入尺寸链 — int 边界溢出 — 严重程度：低-中

- **位置**：`WindowRuleApplicator.cpp:206-209, 236-239`（守卫仅 `VEC->x < 1 || VEC->y < 1`，`NaN<1` 为假 → NaN 放行，inf 放行）；`WindowTarget.cpp:432-446`（表达式变量含 `window_w/window_h/cursor_x/cursor_y`——受客户端 commit/指针影响的量）；`src/helpers/math/Expression.cpp:55-64`（muParser `Eval` 除零按 muParser 默认返回 inf 而非抛异常，外层 try 捕获不到）；静态侧 `Window.cpp:1466-1473`（`size` 表达式结果无有限性检查直接 `setPseudoSize`）；下游 `WindowTarget.cpp:239-241, 418-421`（`Vector2D::clamp` 把 NaN 继续传播，configure 尺寸最终转整型 → UB/巨值）
- **描述**：规则表达式以 muParser 编译，变量绑定到窗口 goal 尺寸、光标坐标等运行期值；求值结果除 `empty()` 与 `< 1` 外没有任何 `std::isfinite`/上限校验。NaN 与 inf 都能通过守卫进入 `COverridableVar<Vector2D>`，随后在 clamp/configure 链路扩散。
- **攻击路径**：用户规则含表达式（wiki 推荐用法，如 `maxsize 20*cursor_y 400`、`size window_w/2 window_h/2`）。攻击者不请求尺寸使 goal 含 0，或诱导光标位置 ⇒ `window_w/0` 型表达式（作者写的除式分母可被客户端拉到 0）⇒ inf 写入 maxSize，`clampWindowSize` 用 inf 正常；若得 NaN（inf/inf）则 `(int)` 转换 UB、configure 出巨尺寸 → 客户端不 ack → resize 挂起，或负尺寸 → 渲染层整数溢出。同时每次客户端 metadata 提交都会触发 `SetExpr` 重新编译表达式（μs 级但可无限频）→ 主线程 CPU 放大。
- **修复**：表达式结果 `std::isfinite` + 合理上限（monitor 尺寸×N）校验，非法即拒绝该 effect；对表达式做 parse-cache（规则文本不变则不重编译），消除高频重编译路径。

---

## F-MATCH-07 ｜tonemap 效应把 propMask 记到 noAutoHDR 头上 → 重载后陈旧 tonemap 覆盖永不解除 — 严重程度：低

- **位置**：`WindowRuleApplicator.cpp:381-384`：`case WINDOW_RULE_EFFECT_TONEMAP: { m_tonemap.first.set(...); m_noAutoHDR.second |= rule->getPropertiesMask(); }`（应为 `m_tonemap.second`，复制粘贴错误）；另 `:125`（`DEFINE_PROP(int, idleInhibitMode, false, …)` 以 bool 初始化 int，顺手修）
- **描述**：`resetProps`（`WindowRuleApplicator.cpp:19-33`）按 `propMask & props` 决定是否 `unset(PRIORITY_WINDOW_RULE)`。tonemap 的 mask 永远为 NONE → 热重载（`clearAllRules` + `updateAllRules`）后即使 tonemap 规则已删除/不再匹配，`m_tonemap` 的 WINDOW_RULE 槽**无人有权清除**，该窗口的色调映射行为永久固化（陈旧特权覆盖）；反向地，noAutoHDR 的 mask 被 tonemap 规则的位污染，删除 noautohdr 类规则时会连带清掉无关的 noAutoHDR 覆盖。
- **攻击路径**：无直接攻击者输入（规则来自信任配置），但属“规则热重载竞态”承诺范围内的陈旧状态缺陷：曾用 tonemap 规则匹配过的窗口，输出管线永久偏离当前配置。
- **修复**：`m_tonemap.second |= rule->getPropertiesMask();`；补一条回归测试：加载 tonemap 规则 → 命中窗口 → 删除规则 reload → 断言 `tonemap().getPriority()` 不再持有 WINDOW_RULE 值。

---

## F-MATCH-08 ｜updateAllRules/属性重扫迭代活容器引用 + 回调重入 → recheck 迭代安全 — 严重程度：低-中

- **位置**：`src/desktop/rule/Engine.cpp:34-48`（`for (w : windowState()->windows())` —— `WindowState.hpp:16` 返回**引用**，容器 `ViewState.cpp:19/24` 会在 manage/unmanage 时增删）；循环体内 `propertiesChanged` 尾部 `WindowRuleApplicator.cpp:619-627` 依次执行 `updateWindowData`、`presentation().updateDecorations/refreshValues`、`forceRecalcFor`、`Event::bus()->m_events.window.updateRules.emit(...)`（**插件回调点**）；图层侧同构 `LayerRuleApplicator.cpp:153`；另 `PropRefresher.cpp:96-103` 第二个循环对 windows() 调 `uncacheDecorations()` 且无 `validMapped` 过滤（updateAllRules 有，此循环没有，未映射窗口仍在容器里）
- **描述**：整轮 recheck 以裸引用遍历全局窗口/图层 vector，循环中段将控制权交给插件与动画/装饰子系统；任何重入路径关闭/新建窗口（`std::erase_if`/`emplace_back`）都使正在进行的迭代失效 → 迭代器悬垂/UAF 或跳窗/重处理。触发面：`hyprctl reloadrules`（`Commands.cpp:2034`）、Lua `rule:set_enabled()`（`LuaWindowRule.cpp:41`，脚本任意时机可调用）、REFRESH_WINDOW_STATES 批量刷新。
- **攻击路径/文字 PoC**：配置中一条 keybind-hyprctl 式链路或某个在 `window-update-rules` 事件里做清理动作的插件（例如“发现某 tag 就 close 窗口”）：触发 `reloadrules` → updateAllRules 迭代中关闭 vector 前方窗口 → `std::vector` 前移收缩，随后 `w->m_ruleApplicator` 取到已析构窗口。至少造成崩溃 DoS，理论上 UAF 可续读虚表。
- **修复**：`for (auto snapshot = windowState()->windows(); ...)` 先拷贝 SP 列表再逐个处理，每个元素处理前重查 `validMapped`；layers 同理；PropRefresher 第二循环补 `validMapped` 守卫。

---

## F-MATCH-09 ｜exec 规则生命周期错位：引擎过期清理后窗口端 m_execRules 永续，且动态重扫不再校验匹配 — 严重程度：低

- **位置**：`WindowRuleApplicator.cpp:151`（`m_execRules` 持 SP）、`:555-560`（readStaticRules 从引擎摘除但窗口端保留）、`:611-617`（propertiesChanged 对 `m_execRules` 重放 `applyDynamicRule`，**无 `matches()` 复检、无过期检查**）；`Engine.cpp:30-32` `cleanExecRules` 只清引擎 vector；`Rule.cpp:146-164`（1 分钟 `expiresAt` 仅由 clean 消费）；`WindowRule.cpp:466-468`（`EXEC_TOKEN` 匹配在 `allowEnvLookup=false` 时直接 `break` —— 视为通过；当前 config 无法注册裸 EXEC_TOKEN 故未开放，但 `registerMatch` 为 public，插件面即 fail-open 原形）
- **描述**：一次性 exec 规则命中并被 `readStaticRules` 消费后：引擎侧到期即删，但命中的窗口 Applicator 永久持有 SP 副本，每次 `propertiesChanged` 的 `resetProps` 清掉其动态效应后又原样重放——本应 1 分钟过期的“一次性”效应在该窗口存续期内事实上永久、且不受热重载 `clearAllRules` 管辖（窗口还持有强引用）。同时重放不重校验 token/pid（窗口 title、pid 关联早已变化也无所谓）。
- **攻击路径**：依赖 F-RULE-02 的 token 冒领或诱导启动链拿到一次 exec 规则命中，即可让 `nofocus`/`pin`/`nofollowmouse` 等动态效果在该窗口上赖到窗口关闭为止，跨多次配置重载仍存活。
- **修复**：`propertiesChanged` 重放 `m_execRules` 前检查 `execExpired()` 并 `m_execRules` 中剔除；`!allowEnvLookup` 时 EXEC_TOKEN case 改为 `return false`（fail-closed），若需要跳过则要求 `isExecRule()` 显式声明。

---

## 已检查、未见新高危的面（负面结果记录）

- `CRegexMatchEngine`（negative: 前缀、FullMatch）：核心缺陷即已报 F-RULE-01，不重复；其余行为（构造期解析、`!= m_negative` 组合）无新增可利用面。
- `CBoolMatchEngine`/`truthy()`（MiscFunctions.cpp:465-480）：前缀匹配 `"on..."`→true 略宽（`"once"` 为真），影响仅限配置作者笔误，不单列。
- `CWorkspaceMatchEngine` → `matchesStaticSelector`：选择器来自信任配置，窗口→ws 归属性由合成器管理，客户端无直接操纵面。
- `setsIntersect`（SetUtils.hpp）：交换递归深度≤1，无栈风险；比较语义正确。
- `IEffectContainer`（EffectContainer.hpp）：动态效应名去重返回既有 id——两插件注册同名效应会共享槽位，属插件生态问题，非本域安全缺陷；`storageType` 溢出有 `numeric_limits::max()` 防护。
- `Rule.cpp` 的 `MATCH_PROP_STRINGS`/`RULE_ENGINES` 编译期排序与 `lower_bound` 查表：一致、无越界。
- `ReservedArea.cpp`（域内点名）：纯几何运算，输入来自 monitor/layer 已钳位的 strut 值；`applyip` 可使 w/h 为负但上游 `CReservedArea` 构造已 clamp ≥0 且 addType 增量 clamp，未见由客户端 strut 直达的危险放大（strut 校验在 layer 协议域）。
- Lua `hl.window_rule/hl.layer_rule` 的参数校验/幂等重注册细节归 config-lua 域（其暴露的注册面问题已并入 F-MATCH-01 的引擎侧修复）。
- `recheckStaticRules` 未重置 `static_`（`suppress_event` 列表在 recheck 轮会重复追加）：仅无界增长隐患，量级过小不单列。
