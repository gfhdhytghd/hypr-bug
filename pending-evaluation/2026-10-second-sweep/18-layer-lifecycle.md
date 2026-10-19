# 18 — Layer-shell 表面生命周期与锁屏图层审计

- 基线:`main @ e38076ac9`(Hyprland fork)
- 审计域:`src/desktop/view/LayerSurface.{cpp,hpp}`、`src/desktop/state/LayerState.cpp`、`src/desktop/state/LayerFadeout.{cpp,hpp}`、`src/desktop/state/FadingOutState.cpp`、`src/desktop/view/animationControllers/LayerSurfaceAnimationController.cpp`,以及 monitor 销毁 / 会话锁 / 焦点路径与 layer 的交叉面(`src/output/Monitor.cpp`、`src/render/Renderer.cpp`、`src/managers/SessionLockManager.cpp`、`src/managers/input/InputManager.cpp`、`src/desktop/state/FocusState.cpp`、`src/desktop/state/ViewHitTester.cpp`、layerrule 匹配面)
- 边界:06 域已覆盖 `protocols/LayerShell.cpp` 的协议请求校验面([F-PROTO-05/08/10]),本文只报**对象生命周期 / 状态机 / 锁屏语义**问题。
- 威胁模型:恶意 layer 客户端(bar/OSD 类),以及由环境(hotplug/DPMS)触发、由恶意客户端维持的驻留状态。
- 指针语义前提:`PHLLSREF = WP<CLayerSurface>`(`src/desktop/DesktopTypes.hpp:35`),`m_layerSurfaceLayers` 为弱引用容器,强引用唯一持有者是 `CLayerState::m_layers`(`LayerState.cpp:15`),失效清除点唯一在 `CLayerSurface::~CLayerSurface`(`LayerSurface.cpp:99-103`)。

---

## F-LAY-01 ｜layer namespace 可伪造 → above_lock 特权效果与锁屏上层输入被冒名取得 — 高危

**位置**
- `src/desktop/rule/layerRule/LayerRule.cpp:105-122` — `CLayerRule::matches()` 的唯一匹配键是 `RULE_PROP_NAMESPACE`(客户端在 `get_layer_surface` 里自由填写的字符串),无任何客户端身份(PID/凭证/security-context)校验。
- `src/desktop/rule/layerRule/LayerRule.cpp:40-45` — `above_lock` 取值 clamp 到 0..2;**2 = 允许锁屏上层接收指针输入**。
- `src/render/Renderer.cpp:990-993` — `above_lock != 0` 的 layer 在锁屏 pass 中渲染于 lock surface **之上**。
- `src/desktop/state/ViewHitTester.cpp:341-345` — 锁屏期间仅 `above_lock == 2` 的 layer 参与 pointer hit-test。
- 对照组:`src/desktop/rule/layerRule/LayerRuleApplicator.cpp:108-111`(aboveLock 效果经普通 rule 通道注入,`propertiesChanged` 每次 rule reload / map 时重新按 namespace 匹配)。

**攻击路径**
用户为锁屏通知类 OSD(典型:`hyprland-disk-notify`、battery warn、`hyprlock` 自有 overlay)配置了 `layerrule = namespace,^mylockosd$, above_lock, 2`。任意非特权 layer 客户端在**任意时刻**(包括锁屏进行中)创建 `zwlr_layer_surface_v1` 并填入相同 namespace:
1. 恶意层立即获得 `above_lock=2` 效果 → 锁屏时渲染于 lock surface 之上(Renderer.cpp:1762-1771 的 lock pass);
2. 同时通过 ViewHitTester 的锁屏输入白名单 → 获得 pointer enter/motion/button;
3. 在 lock 输入区之上绘制假“Enter PIN/密码”对话框,拦截用户点击(键盘不会进假层——`rawSurfaceFocus` 的锁检查 `FocusState.cpp:235` 挡住键盘——但真 lock surface 的点击被截走,配合视觉欺骗足以完成钓鱼);
4. 同一冒名还顺带取得 `order` 规则带来的 z 序特权(排序见 `Renderer.cpp:2792-2795`)。

**文字 PoC**
```
// 恶意客户端,锁屏已激活时执行
registry.bind(zwlr_layer_shell_v1)
surf = compositor.create_surface()
ls   = layer_shell.get_layer_surface(surf, output, layer=overlay, namespace="mylockosd") // 冒名
ls.set_anchor(top|bottom|left|right)  // 全屏
frame.callback(); surf.attach(buf=假密码框); surf.commit()
// → 合成器按 namespace 命中用户 above_lock=2 规则:
//   锁屏上层绘制 + pointer 可点,用户点击落在攻击者 surface
```

**修复建议**
- 给“安全敏感效果”(`above_lock`,以及可选的 `order` 高值)增加独立于 namespace 的确认因素:layerrule 新增 `client`/`pid`/`executable`(经 `wl_client_get_credentials` 或 security-context 匹配);或
- `above_lock >= 1` 的规则默认不命中,需要用户在 rule 中显式加 `trust,` 标记且命中时记录 LOG(WARN) 审计日志;至少将 `above_lock=2`(输入)与 `=1`(仅渲染)拆分两个独立配置面,输入类效果强制要求非 namespace 匹配键。

---

## F-LAY-02 ｜未映射 layer 继续占用 exclusive zone 与列表槽位 → 隐形工作区 DoS(记账泄漏)— 中高危

**位置**
- `src/render/Renderer.cpp:2694-2704` — `arrangeLayerArray` 跳过条件只有 `!ls || !ls->m_layerSurface || (m_flags & LAYER_FLAG_DEAD)`,**没有 `mapped()` 检查**;`m_current.exclusive > 0` 的未映射层照常走 `applyExclusive`(`Renderer.cpp:2771`)。
- `src/protocols/LayerShell.cpp:74-80` — bufferless commit 走 unmap:`m_mapped=false; m_configured=false`,但 `m_current`(含 exclusive/anchor/margin)保留,layer 也不被移出 `m_layerSurfaceLayers`。
- `src/desktop/view/LayerSurface.cpp:235-298` — `onUnmap` 仅创建 fadeout/重排,不触碰 exclusive 记账。

**攻击路径**
恶意 bar 客户端:
1. 创建 TOP layer,`set_exclusive_zone(2000)`,锚 top,提交一帧(合法 map,用户短暂看到);
2. 提交空 buffer(unmap)→ 表面消失,但条目仍在 `m_layerSurfaceLayers[TOP]`,状态机停在“未映射但参与 arrange”;
3. 此后**每一次** `arrangeLayersForMonitor`(`LayerSurface.cpp:154/194/297/308/354`、`Renderer.cpp:174`)都继续为这个不可见层扣减 2000px usable area;
4. 所有窗口(tiling、fullscreen 计算、`m_reservedArea` RESERVED_DYNAMIC_TYPE_LS)被永久压扁成残余缝隙;客户端只要不 destroy 资源、也不再 map,状态无限保持。与 F-PROTO-08 的 serials 泄漏协同:每次 arrange 若尺寸抖动(`Renderer.cpp:2773-2774`)还向该死层 `configure()` 刷 `m_serials`。

**文字 PoC**
```
ls = get_layer_surface(surf, out, top, "evilbar")
ls.set_anchor(top|left|right); ls.set_exclusive_zone(2000); ls.set_size(1920,200)
wait configure-ack; attach(buf); commit()      // map
attach(NULL); commit()                          // unmap,界面干净
// 之后 :hyprctl workarea 显示高度 -2000;所有新窗口在缝隙里布局
```

**修复建议**
`arrangeLayerArray` 两个 pass 均加 `if (!ls->mapped()) continue;`;或在 `onUnmap` 中把 `m_geometry` 清零并在 `m_layerSurface->m_current.exclusive` 上引入 “unmapped 视为 0” 的 arrange 侧读态(不改协议 current)。同时在 unmap 后停止对该资源 `configure()`(`Renderer.cpp:2773` 前加 mapped 检查)。

---

## F-LAY-03 ｜unmap 不回收 popup 树的键盘焦点,onUnmap 还会反向“再武装”焦点 — 中高危

**位置**
- `src/desktop/view/LayerSurface.cpp:274` — `WASLASTFOCUS` 只比较 **主 surface** `m_wlSurface->resource()`;对比 `onCommit` 的 interactivity 分支(`LayerSurface.cpp:385-394`)会 breadthfirst 遍历 surface 树 **和 popupHead**。
- `src/desktop/view/LayerSurface.cpp:285-286` — else-if 用 `g_pSeatManager->setKeyboardFocus(Desktop::focusState()->surface())` 直接设 seat 焦点:当 `focusState()->surface()` 正是该 layer 的 popup surface 时,等于 unmap 后**把键盘焦点又指回隐身 popup**。
- `src/desktop/view/Popup.cpp:95-96` — `CPopup::mapped()` 链式依赖 parent:parent 未映射后 popup 不再被渲染(`Renderer.cpp:987` 门控)也不再被 `layerPopupSurfaceAt` 命中(`ViewHitTester.cpp:325`),但**seat 键盘路由不做该检查**,且 `CWLSurface::keyboardFocusable()` 对 `VIEW_TYPE_POPUP` 恒真(`src/desktop/view/WLSurface.cpp:203-208`)。
- `src/desktop/view/LayerSurface.cpp:147` — 只有 `onDestroy` 才 `resetPopupHead()`;`onUnmap` 完全不处理 popup 焦点。

**攻击路径**
1. 恶意 OSD 客户端建 ON_DEMAND 交互 layer,`get_popup` 挂一个 `input_region=infinite` 的透明 popup(覆盖全屏);
2. 用户任意位置单击 → `layerPopupSurfaceAt`(`InputManager.cpp:434-435`、`ViewHitTester.cpp:322-338`)命中 popup → `rawSurfaceFocus(foundSurface)`(`InputManager.cpp:703-705`)→ 键盘焦点进入攻击者 popup;**按键全部被中间人收割**(视觉上是正常桌面)。
3. 受害者/攻击者触发 parent unmap(bufferless commit):`onUnmap` 的 `WASLASTFOCUS=false`(focus 是 popup 不是主面),走到 285-286 反而 `setKeyboardFocus(popup)` 再次确认 —— 键鼠事件持续流向“不可见、逻辑上已不存在”的 popup,直到下一次真实焦点变更。

**文字 PoC**
```
ls = get_layer_surface(surf, out, top, "kbd-steal"); kbd_interactivity=on_demand
p_surf = compositor.create_surface()
p_surf.set_input_region(wl_region_infinite)
ls.get_popup(p_surf.resource); xdg_popup 提交透明 buffer → 全屏可见输入区
// 用户单击 → popup 获得键盘焦点;攻击者 parent 再 commit(NULL):
//   onUnmap: WASLASTFOCUS(main)==false → line 285 → setKeyboardFocus(popup) 复位
//   之后所有按键到达攻击者 popup,而屏幕看不到任何攻击者表面
```

**修复建议**
`onUnmap`(及 `onDestroy` 强制路径)复用 `onCommit` 的遍历:收集 `m_wlSurface->resource()` 树 + `popupHead()->breadthfirst`,若任一 surface == `g_pSeatManager->m_state.keyboardFocus`,则 `rawSurfaceFocus(nullptr)` 后再 refocus;禁止 285-286 把焦点设回属于本 layer 子树的 surface。popup 视图也应在 parent unmap 时统一 unmap。

---

## F-LAY-04 ｜fadeout 期间重提交:旧快照对象不失效,鬼影内容驻留并封锁 direct scanout — 中危

**位置**
- `src/desktop/state/LayerFadeout.hpp:18-27` / `LayerFadeout.cpp:44-103` — `CLayerFadeout` **不持有 layer 的任何 WP/SP**,只拷贝 snapshot、geometry、zIndex;与 `CWindowFadeout` 不同,没有“拥有者仍映射则取消”的途径。
- `src/desktop/state/FadingOutState.cpp:13-30` — 移除条件仅 `done()`(alpha 归零)或 monitor 过期;`add()` 无去重、无 per-layer 键。
- `src/protocols/LayerShell.cpp:67-72` — 客户端随时可再次 attach+commit 重新 map(`m_configured` 流程允许);`LayerSurface.cpp:175-233` `onMap` 重放 `animateIn`,不触碰 fadingOutState。
- `src/output/Monitor.cpp:1842-1849` — 任何属于该 monitor 的 fadeout 使 `isSolitaryBlocked` 置 `SC_FADEOUT` → direct scanout/tearing 被挡。

**攻击路径**
恶意通知层:配置 `fadeLayersOut` 时长内(或诱导长动画)执行 `commit(NULL) → commit(buf)` 循环。每轮 unmap 生成一个 monitor 尺寸 FB 快照的 fadeout(`Renderer.cpp:3244-3292`,无速率限制),重 map 后旧快照仍按旧几何/平面叠在新内容上:
1. 视觉鬼影:已“关闭”的敏感内容(通知正文、验证码)滞留到动画结束,可无限续期形成**内容持久化**;
2. `m_fadeouts` 与 AnimationManager 动画对象按提交速率增长(每个 fadeout 3 个 AVar,`LayerFadeout.cpp:78-80`),内存/GPU FB 泄漏式放大;
3. 期间该 monitor 永远拿不到 direct scanout(功耗/延迟回归),且 `isSolitaryBlocked` 影响 DPMS/省电路径。

**文字 PoC**
```
每 100ms 交替: attach(NULL)+commit; attach(buf(不同内容))+commit
// fadeLayersOut 设 10s(常见观感配置)时,fadingOutState 同时挂 ~100 个
// CLayerFadeout,屏幕残留 10 秒前的“已关闭”内容;monitor 永不 solitary
```

**修复建议**
`IFadeout` 增加 `WP<CLayerSurface> owner()`;`CFadingOutState::add` 时若已存在同 owner 的未完成 fadeout 则替换/合并;`CLayerSurface::onMap` 中 `std::erase_if` 掉本层的 pending layer fadeouts。另对单客户端未映射速率 / 在队 fadeout 数设上限。

---

## F-LAY-05 ｜monitor 销毁只 sendClosed 不 unmap:幽灵 layer 保住键盘焦点并全局封锁窗口焦点 — 中高危

**位置**
- `src/output/Monitor.cpp:435-441` — `onDisconnect` 对 `m_layerSurfaceLayers` 仅 `sendClosed()` + `clear()`;**不调用 onUnmap/onDestroy、不动 seat 焦点、不动 `m_exclusiveKeyboardLSes`**(客户端可以永远不响应 closed)。
- `src/desktop/view/LayerSurface.cpp:110-112` — `mapped()` 只看 `m_mapped && m_layerSurface->m_mapped && resource`,与 monitor 无关 → layer 在 monitor 死后仍然 “mapped”。
- `src/desktop/state/FocusState.cpp:111-114` + `src/managers/input/InputManager.cpp:1831-1834` — `m_exclusiveKeyboardLSes` 是**全局**(非 per-monitor)向量:条目仍指向这个已经无处渲染的 EXCLUSIVE layer 时,所有 monitor 上 `refocusLastWindow` 返回 false、`rawWindowFocus` 拒绝聚焦窗口;键鼠键盘事件继续路由到 `g_pSeatManager->m_state.keyboardFocus`(其表面仍归已死 monitor 上的攻击者)。
- `src/output/Monitor.cpp:450-457` — 仅无 BACKUPMON 时 reset focusState;有 BACKUPMON(常见)时既不 refocus 也不清 seat keyboardFocus(onDisconnect 全程无 refocus,grep 证实)。
- 次级:`Monitor.cpp:436-438` 对 `PHLLSREF`(WP)直接 `ls->m_layerSurface` 解引用、`ViewHitTester.cpp:344` 对 `ls->mapped()` 无 `!ls` 先行判断 —— 依赖“析构函数必清除”的脆弱不变量(`LayerSurface.cpp:96-97` 的 `monitorState()` 空指针早退会破坏它)。

**攻击路径**
1. 恶意客户端在副显示器创建 `EXCLUSIVE` 键盘交互 layer 并 map(键盘按协议抢入 `m_exclusiveKeyboardLSes`,`LayerSurface.cpp:198-201`);
2. 受害者拔线 / DPMS off / `hyprctl monitor ... disable`(环境触发,无需恶意);
3. 结果:该 layer 对象存活于 `CLayerState::m_layers`(hyprctl/jobs 视角幽灵),`m_mapped==true`,列表条目未清除 → **整个合成器**(含主显示器)无法把键盘焦点还给任何窗口;受害者按键持续进入攻击者表面。攻击者只需保持连接即无限驻留。

**文字 PoC**
```
// 攻击者(副屏 eDP-2 上)
ls = get_layer_surface(surf, out2, overlay, "evil-exclusive")
ls.set_keyboard_interactivity(EXCLUSIVE); map()   // 进入 m_exclusiveKeyboardLSes
// 受害者: xrandr 式禁用 / 物理拔出 out2
// → Monitor::onDisconnect: sendClosed 被忽略;条目+焦点保留;
//   FocusState::rawWindowFocus 永远 "Refusing ... exclusive ls"
```

**修复建议**
`onDisconnect` 遍历该 monitor 的 layer,执行与 `onUnmap` 等价的完整拆解(`m_mapped=false`、surface unmap、exclusive 列表擦除、子树焦点命中则 `rawSurfaceFocus(nullptr)`),并把 `m_exclusiveKeyboardLSes`/焦点判定改为按 monitor 有效层过滤;对 `ls` 的解引用前补 `expired()` 守卫。

---

## F-LAY-06 ｜onMap 无条件重设 LAYER_FLAG_ABOVE_FULLSCREEN,可绕过独占全屏降级 — 中危

**位置**
- `src/desktop/view/LayerSurface.cpp:175-180` — `onMap` 每次映射无条件下 `m_flags |= LAYER_FLAG_ABOVE_FULLSCREEN`(对 BACKGROUND/BOTTOM 也设);降级唯一发生在 layer-change commit(`LayerSurface.cpp:339-342`)与 FS 进入时的外部清除(`src/managers/fullscreen/handler/FullscreenHandler.cpp:292-297`、`Monitor.cpp:1464/1522/1581/1600`、`ScrollingFullscreenHandler.cpp:350/433`)。
- 消费者:`src/managers/input/InputManager.cpp:463-475` — 独占全屏时,`TOP && !(flag)` 的 layer 视为不可聚焦被丢弃;flag 复位后 TOP 层重新可命中/可聚焦;alpha 强制归零只在 layer-change 分支(`LayerSurface.cpp:344-345`)做,`onMap` 的 `animateIn` 直接把 alpha 拉到 1(`LayerSurfaceAnimationController.cpp:115-122`)。

**攻击路径**
全屏游戏/播放器(internal FSMODE_FULLSCREEN)激活时,恶意 TOP bar 反复 `commit(NULL) → commit(buf)`(map→unmap→map):每次 map 都把被 FullscreenHandler 清掉的 flag 又点亮,且 `animateIn` 无视 `hasFullscreen` → 层可见+可点击,压在独占全屏内容之上(点击劫持/广告覆盖),不需要任何规则配合。

**文字 PoC**
```
// FS 激活后,周期性:
attach(NULL); commit()            // unmap
attach(buf_with_fake_overlay); commit()  // onMap: flag 复位 + alpha 0→1
// InputManager.cpp:469 的 TOP-layer 屏蔽失效
```

**修复建议**
`onMap` 中按 `LayerSurface.cpp:339-345` 的同一规则计算 flag/alpha:`hasFullscreen(monitor)` 时仅 `layer >= OVERLAY` 置位并保留 alpha 归零逻辑,避免“map 即升格”。

---

## F-LAY-07 ｜layer-change 重定位依赖值语义 operator== 查找,失败即静默错位;镜像 monitor 取 front() 无空检查 — 低中危

**位置**
- `src/desktop/view/LayerSurface.cpp:330-336` — 换层时线性找 `*it == m_self`;匹配走 `CLayerSurface::operator==(const CLayerSurface&)`(`LayerSurface.hpp:110-112`,比较 `m_layerSurface` 与 `m_monitor` 两个 WP 的“值”)而非指针身份。两个 `m_layerSurface` 均已 reset(destroy 中)的存活对象会互相“相等”→ 可能把**别的** WP 条目搬进新层向量、原位条目残留:新层 z 序错乱 + 旧桶幽灵 WP(要等其宿主析构才被扫掉,期间 `arrangeLayerArray` 虽跳过 `!m_layerSurface`,但 `Renderer.cpp:2792-2794` 的 stable_sort 比较器仍会 `a->` 解引用容器里的每个条目)。
- `src/desktop/view/LayerSurface.cpp:54-55` — `pMonitor->m_mirrorOf` 时 `State::monitorState()->monitors().front()` 无 empty 检查、且可能选中镜像自身(镜像也在 monitors() 中)→ layer 挂到 mirror monitor 上,`damageMonitor` 对 mirror 直接 return(`Renderer.cpp:2889-2890`)→ 永不重绘的“黑洞 layer”。
- `src/desktop/view/LayerSurface.cpp:304-311` — 未映射重提交分支用 `monitorID()`(可能 `MONITOR_INVALID`)调 `arrangeLayersForMonitor`,当前靠 `query().id()` 返回 null 兜底,属侥幸。

**攻击路径**
客户端在 destroy 竞态窗口(资源 `destroy` 与 `CLayerState` 强引用释放之间)反复 `set_layer` 提交:换层查找可搬错条目/留下残留,残留过期 WP 进入 sort 比较器解引用路径。

**修复建议**
重定位比较改用 `it->lock().get() == this` 指针身份;`create()` 中镜像重定向加 `monitors().empty()` 守卫并排除 mirror 自身;`onCommit` 的 INVALID monitor 早退。

---

## F-LAY-08 ｜onUnmap 缺 `m_mapped` 重入守卫;closelayer/layer.closed 双源事件可被再触发 — 低危(加固)

**位置**
- `src/desktop/view/LayerSurface.cpp:235-298` — `onUnmap` 无 `if (!m_mapped) return;` 开头守卫;`IPC postEvent("closelayer")` 与 `Event::bus()->m_events.layer.closed.emit`(`LayerSurface.cpp:238-239`)在函数最前部,**先于**所有状态判断执行。
- 事件源有两处:`src/protocols/LayerShell.cpp:76`(commit 无 buffer 路径直接 emit)与 `LayerShell.cpp:41`(转发 `surf_->m_events.unmap`,由 `Compositor.cpp:307-311` 的 destroy 路径 emit)。当前靠 `CWLSurfaceResource::unmap()` 不发事件(`Compositor.cpp:562-578`)侥幸不双发;任何后续改动(如 XWayland 式 unmap 转发、destroy 前已 unmap 的层再被 forced unmap)都会造成:`layer.closed` 双发(插件打开/关闭计数失衡)、`makeSnapshotFB` 对已 unmap 层重跑(`LayerSurface.cpp:258` → `Renderer.cpp:3282` renderLayer 返回空,却仍分配整屏 FB 并塞入一个空 fadeout)、二次 refocus。

**攻击路径**
今天可复现的最小效果:客户端让 layer 处于 “surface destroy 时仍 mapped” 状态(不发显式 unmap,直接 destroy surface):`Compositor.cpp:309` 先发 unmap → `onUnmap` 建快照;随后 destroy 广播 → `onDestroy`(`LayerSurface.cpp:133`)——顺序敏感路径全部依赖 `m_mapped` 单点状态;建议在状态机入口加显式幂等守卫,而非依赖上游 emit 次数。

**修复建议**
`onUnmap` 开头加 `if (!m_mapped) return;`;把 IPC/EventBus 通知移到 `m_mapped` 判定之后。

---

## 汇总

| 编号 | 标题 | 严重程度 |
|---|---|---|
| F-LAY-01 | namespace 冒名取得 above_lock(锁屏上层输入/绘制特权) | 高 |
| F-LAY-02 | 未映射 layer 继续占用 exclusive zone(隐形工作区 DoS) | 中高 |
| F-LAY-03 | unmap 不回收 popup 键盘焦点且反向再武装焦点(按键中间人) | 中高 |
| F-LAY-05 | monitor 销毁遗留幽灵 EXCLUSIVE layer → 全局键盘焦点封锁 | 中高 |
| F-LAY-04 | fadeout 期间重提交:旧快照不失效,鬼影+scanout 封锁 | 中 |
| F-LAY-06 | onMap 无条件点亮 ABOVE_FULLSCREEN,绕过独占全屏降级 | 中 |
| F-LAY-07 | 换层查找用值语义 ==;镜像 front() 无空检查 | 低中 |
| F-LAY-08 | onUnmap 无幂等守卫,双源 unmap 事件面 | 低(加固) |
