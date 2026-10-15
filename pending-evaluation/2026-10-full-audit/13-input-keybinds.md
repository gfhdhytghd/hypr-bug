# 13 — 输入设备与键绑定执行链审计

- 仓库：/home/wilf/data/Hyprland（fork，main @ 397cb0efb，基线 v0.56.0）
- 审计域：`src/devices/`、`src/managers/input/`（InputManager / Touch / Tablets / Eis / InputMethodRelay）、`src/keybinds/`、以及与其交叉的 `src/config/lua/bindings/`、`src/config/supplementary/executor/`、`src/protocols/{VirtualKeyboard,VirtualPointer,InputMethodV2}.cpp`
- 说明：本 fork 无 `src/helpers/input/`（键码/修饰符状态机在 `src/devices/IKeyboard.cpp`）；键绑定回调整体走 Lua（`hl.bind` → `CBind::make`，`src/config/lua/bindings/LuaBindingsToplevel.cpp:229`）。
- 威胁模型回顾：恶意 Wayland 客户端诱导合成器执行键绑定语义；USB/虚拟设备销毁竞态；锁屏期间触发任意 dispatcher。
- 交叉引用：virtual-keyboard keymap mmap 崩溃（F-PR2-01）、IME 首绑劫持（F-PR2-02）已由同事报于 `07-protocols-misc.md`，本文不重复计数，只做输入链上的增量分析。

锁屏门控总览（正面结论）：键绑定唯一执行闸门是
`CKeybindManager::canInvokeNow()`（`src/keybinds/Manager.cpp:897-908`，含
`g_pSessionLockManager->isSessionLocked() && !bind->hasFlag(BIND_FLAG_LOCKED)`）。
`processEvent` 候选循环（Manager.cpp:470）、repeat/long-press 定时器（Manager.cpp:82, 116）、`invokeDeferredBinds`（Manager.cpp:655）均正确经过该闸门；switch（Lid）事件经 `onSwitchEvent → processEvent` 同样受控；EIS（InputCapture 模拟输入，`src/managers/input/Eis.cpp`）完全不处理 key/button 注入事件（`onEvent` 只处理连接/座位生命周期），不构成注入面。
**但存在两条绕过 canInvokeNow 的执行路径（F-INP-01/02）**，以及多处设备销毁生命周期缺陷。

---

## F-INP-01（Critical）— 任意 Wayland 客户端经 virtual-keyboard 触发用户键绑定（含 exec 类 dispatcher）→ 会话内任意命令执行

- 位置：
  - `src/protocols/VirtualKeyboard.cpp:156-177`（`onCreateKeeb`，全局无门控；`src/protocols/WaylandProtocol.cpp:24` `wl_global_create` 对所有客户端广播）
  - `src/managers/input/InputManager.cpp:1680-1703`（`onKeyboardKey`：仅当客户端是"正在 grab 的 IME"才 `DISALLOWACTION`；普通客户端的虚拟键盘 key 事件照常进入 `Keybinds::mgr()->onKeyEvent`，Manager 侧无 virtual 判定）
  - `src/managers/input/InputManager.cpp:1802-1815`（`shouldIgnoreVirtualKeyboard` 只屏蔽 IME-grab 客户端自己）
  - `src/managers/permissions/DynamicPermissionManager.cpp:222-224`（"keyboards are allow default"：PERMISSION_TYPE_KEYBOARD 默认放行，无用户弹窗）
  - `src/devices/VirtualKeyboard.cpp:49` + `src/config/values/ConfigValues.cpp:436`（`input:virtualkeyboard:share_states` 默认 2=auto：非 IME-grab 客户端的虚拟键盘参与全局按键/修饰符合并，`shareKeyFromAllKBs`/`shareModsFromAllKBs`，InputManager.cpp:1927-1964）
  - 执行终点：`src/keybinds/Manager.cpp:587-604 invokeBind → Bind::invoke`；本 fork 中 exec 类 dispatcher（`src/config/lua/bindings/LuaBindingsDispatchers.cpp:180-205` `exec_cmd/exec_raw` → `CExecutor::spawn` → fork/exec，`src/config/supplementary/executor/Executor.cpp:178`）都由键绑定回调直达。
- 攻击路径：恶意客户端 bind `zwp_virtual_keyboard_manager_v1`（无需权限、无弹窗，keyboard 权限默认 ALLOW 且按设备名字符串记忆——`misc:name_vk_after_proc=0` 时所有 vkbd 同名 `hl-virtual-keyboard`，一次授权全体共享）→ `set_keymap`（自带 xkb keymap）→ `set_modifiers`（伪造 Super 掩码，经 shareModsFromAllKBs 并入 `m_lastMods`）→ `set_key`(Super+T) → 合成器按用户真实配置匹配键绑定 → 执行绑定动作。用户配置里只要有 `exec`/`exec_raw` 语义的绑定，即获得以桌面会话用户身份执行任意命令 = Wayland 沙箱逃逸。即使用户没有 exec 绑定，也可触发 workspace 切换、kill 窗口、launcher 等任意配置语义。
- PoC 思路（文字）：恶意客户端用客户端提供的 keymap 建 vkbd；先发 `modifiers(depressed=Super位)`，再按 keymap 里映射到目标 keysym 的 keycode 发 press/release；对比物理按键行为即可确认绑定被触发。全程无需键盘焦点、无需用户交互。锁屏前同样可用（canInvokeNow 只挡锁定期间）。
- 修复建议：(1) 默认将非 IME-grab 客户端的虚拟键盘视为不可信：`m_allowBinds` 对 virtual 设备默认 false（仅 config 显式 `keybinds=1` 才允许），或至少走 PERMISSION_TYPE_KEYBOARD 弹窗而非默认 ALLOW（删除 DynamicPermissionManager.cpp:223 的键盘默认放行对 virtual 的适用）；(2) `share_states` 默认值对非受信 vkbd 取 0；(3) 键名记忆应含 wl_client 身份（pid+fd 序号），避免同名设备权限串用；(4) `canInvokeNow` 增加 `keyboard->isVirtual() && !trusted` 判定。
- 附注：同一路径下 `handleInternalKeybinds`（Manager.cpp:916 起）在锁屏门控之外处理 `XF86Switch_VT_*` keysym，vkbd 客户端可在锁屏期间强制 VT 切换（黑屏 DoS、干扰解锁）；VT 处理应仅限物理键盘（`isVirtual()` 过滤）。

---

## F-INP-02（High）— 锁屏/会话失活绕过：release 回调链完全绕过 `canInvokeNow`，可被客户端以"销毁设备"为扳机

- 位置：
  - `src/keybinds/Manager.cpp:440-445`（`onDeviceRemoved → invokeReleaseCallbacks(takeReleaseCallbacksForDevice)`，无锁屏检查）
  - `src/keybinds/Manager.cpp:624-639`（`invokeReleaseCallbacks` 直接 `invokeBind`，全程不查 `canInvokeNow`）
  - `src/keybinds/Manager.cpp:461-465 + 549-553`（松开按键时，`releaseFollowUps` 里的绑定被显式豁免 `canInvokeNow`：`if (!releaseFollowUps.contains(bind) && !canInvokeNow(bind)) continue;`）
  - `src/keybinds/Manager.cpp:190-194`（会话非激活分支 `invokeReleaseCallbacks(takeAllReleaseCallbacks())` 同样裸放行）
  - 布防来源：`src/keybinds/Manager.cpp:606-613`（bind 执行返回 `BIND_FOLLOW_UP_TRIGGER_RELEASE` 时写入 `pressedInput->releaseCallbacks`）；产生 followUp 的唯一途径 `src/config/lua/bindings/LuaBindingsToplevel.cpp:233-242` + `src/config/lua/bindings/LuaBindingsDispatchers.cpp:35-37`（`requestBindRelease`；内置 Lua dispatcher `pass`/`global`/`keybind`/`mouse("movewindow")`/`mouse("resizewindow")` 均设置，见 226/251/406/665/673 行）。
- 攻击路径：
  1. 攻击者（或用户）通过 virtual keyboard 注入一个键并按住不放（vkbd 默认 `release_pressed_on_close=false`，`src/protocols/VirtualKeyboard.cpp:135-141`，销毁时不会替攻击者释放按键）。该绑定若来自 Lua 配置且其动作调用了 `requestRelease`（movewindow/resizewindow 等常用封装），则其 `releaseCallbacks` 已布防。
  2. 用户锁屏（hyprlock）。锁屏不清理 `CInputState`（`SessionLockManager.cpp` 无任何 inputState/键态清理，grep 证实）。
  3. 攻击者销毁自己的 vkbd resource（Wayland 客户端销毁自身对象不受锁屏限制）→ `destroyKeyboard → Keybinds::mgr()->onDeviceRemoved → invokeReleaseCallbacks → invokeBind`：**在锁定状态下执行了该绑定动作（任意 Lua dispatcher，包括 exec 类）**。
  变体 A：无需 vkbd——用户在解锁状态下按住某键触发上述绑定，锁屏发生在按住期间，用户松手 → 行 461-465 的 release 分支豁免门控，dispatcher 在锁定状态执行。
  变体 B：物理键盘持键期间被拔（USB 拔除竞态）→ 同一 `onDeviceRemoved` 裸放行链。
  变体 C：VT 切走（session inactive）后，任何 vkbd 按键事件到达行 190-194 分支 → 全量 release 回调被批量执行。
- PoC 思路（文字）：目标用户配置含 `hl.bind{mods={"SUPER"}, key="M", callback=function() hl.actions.mouse("movewindow") end}` 之类的 requestRelease 绑定。攻击模块：建 vkbd → set_keymap（含 SUPER 映射）→ 按上述序列注入 SUPER+M 并保持按下 → 等待 LockNotify 全局（人人可 bind）通知锁定 → `destroy()` 自身 vkbd → 观察到锁定态窗口被移动/目标 dispatcher 生效。
- 修复建议：`invokeReleaseCallbacks` 内对每个 `release.bind` 复查 `canInvokeNow(bind)`（至少复查 `isSessionLocked()` 与 `enabled()`）；`releaseFollowUps` 豁免应改为"布防时快照 + 执行时再闸"；锁屏 `onLocked` 时调用 `Keybinds::mgr()->clearAllInputState()`（含布防回调的丢弃策略需评审——对 mousebind 应合成 release 而非执行）；`onSessionActivity(false)` 路径同理。

---

## F-INP-03（High）— CTouchDevice 销毁顺序错误：emit 后继续使用 `this` → 触摸设备拔出竞态 UAF 写

- 位置：`src/devices/TouchDevice.cpp:17-21`
  ```cpp
  m_listeners.destroy = m_touch->events.destroy.listen([this] {
      m_events.destroy.emit();   // → InputManager destroyTouchDevice → erase 最后一个 SP → 对象析构
      m_touch.reset();           // ← 此后 this 可能已释放：UAF 写
  });
  ```
  对照兄弟类均为安全顺序（先 reset 再 emit）：`Keyboard.cpp:26-29`、`Mouse.cpp:22-25`、`Tablet.cpp:72-75`、`VirtualKeyboard.cpp:18-21`。
  所有权证据：`CInputManager::m_touches` 是唯一 SP 容器（`InputManager.hpp:186`），`m_hids` 与 PointerManager 的 `STouchListener::touch` 均为 WP（`PointerManager.hpp:166-176`）→ `destroyTouchDevice`（InputManager.cpp:1573-1579）erase 后引用计数归零，析构发生在 emit 内。
- 攻击路径：libinput 热插拔事件（`events.destroy`）派发链上，触摸设备下线（物理拔除 USB 触摸屏；或 udev 注入/模拟器触发 device removed）→ aq 的 `events.destroy.emit` 正在迭代监听器列表，而监听器宿主对象（含其 `m_listeners` 成员与 `m_touch` WP）在迭代中被销毁 → ① `m_touch.reset()` 写已释放内存；② 正在迭代的 aq 信号监听器节点被析构 → 迭代器悬空（hyprutils CSignal 若使用惰性删除还好，若即时 unlink 则为二次 UAF）。属于"USB 插拔竞态、设备对象销毁后事件仍路由"类别中最实锤的一处。
- PoC 思路（文字）：带虚拟触摸（libci-hid/ci-vhal 或 fakedriver 热插拔事件流）循环 create/destroy touch device，同时 ASAN 构建运行合成器；ASAN 报 `heap-use-after-free (write) of member CTouchDevice::m_touch`。物理设备：USB 触摸屏在高频触摸中拔线重复。
- 修复建议：与兄弟类对齐——`m_touch.reset(); m_events.destroy.emit();`；更稳妥：InputManager 的 destroy 静态监听器（InputManager.cpp:1993-2000）中先经 `m_self.lock()` 判空（已做），但应把"从 m_touches 移除"推迟到 aq 信号发射栈之外（`g_pEventLoopManager->addTask` / deferred erase），杜绝一切 emit-内析构。

---

## F-INP-04（Medium）— setupKeyboard 事件监听器缺空指针检查：`m_self.lock()` 结果未判空即解引用（destroy 监听器判空，其余全部漏判）

- 位置：`src/managers/input/InputManager.cpp:1138-1173`
  - key 监听器（1138-1146）：`auto PKEEB = keeb->m_self.lock(); onKeyboardKey(event, PKEEB); if (PKEEB->m_enabled) ...` —— `onKeyboardKey` 首行即 `pKeyboard->m_enabled` 解引用（1681），PKEEB 为空直接 SIGSEGV；后续两处 `PKEEB->` 同样裸用。
  - modifiers 监听器（1150-1160）与 keymap 监听器（1162-1173）：`PKEEB->getActiveLayout()`、`PKEEB == g_pSeatManager->m_keyboard` 后 `PKEEB->m_hlName` 等，无判空。对照 destroy 监听器（1128-1136）有 `if (!PKEEB) return;`。
  - 同类：`setupMouse`（1326）`destroyPointer(PMOUSE->m_self.lock())` 允许把 nullptr 传入 destroyPointer（InputManager.cpp:1548+ 对 null 大致容忍，但 `m_currentlyHeldButtons` 清理用 `held.pointer.lock() != mouse` 与 null 比较，行为退化为"全部匹配/不匹配"，建议判空返回）。
- 攻击路径：与 F-INP-03 同族——对象析构与 libinput/协议事件派发交错（键盘拔出、vkbd 客户端 destroy 竞态，事件在 `m_events.destroy` 之后仍被排入同一 dispatch 轮次）时，`m_self` 已过期而 key 信号仍在派发 → 合成器崩溃（锁屏期间可被恶意 vkbd 客户端重复触发 = 抗锁屏 DoS）。当前主要风险是"缺防御"而非必现路径；但同一行号处 `keeb = keeb.get()` 裸指针捕获意味着一旦对象先亡,`keeb->m_self` 本身就是 UAF 读（listenStatic 的静态监听器不会随对方消亡自动断开）。
- 修复建议：所有静态监听器 lambda：lock 后判空 return；`keeb` 用 `WP<IKeyboard>` 捕获而非裸指针；配合 `InputManager` 维护"正在销毁"标记，`destroyKeyboard` 后拒绝再进 `onKeyboardKey`。

---

## F-INP-05（High）— 恶意客户端永久致盲任意物理按键：seat 层 `m_pressed` 去重 + `shareKeyFromAllKBs` 被未释放的 vkbd 持键污染

- 位置：
  - `src/managers/input/InputManager.cpp:1709-1745`（seat 输出去重：`CONTAINS && pressed → return`；`!pressed && ANYPRESSED → return`）
  - `src/managers/input/InputManager.cpp:1927-1951`（`shareKeyFromAllKBs`：任何 `shareStates()` 且未被忽略的键盘——包括任意客户端的 vkbd——只要报"按住"，就吞掉所有人对该键的 release）
  - `src/protocols/VirtualKeyboard.cpp:40-46`（vkbd `m_pressed` 由客户端自管，销毁/断连不清理合成器侧共享状态；`release_pressed_on_close` 默认 false，`ConfigValues.cpp:438`）
  - `src/managers/input/InputManager.cpp:1523-1546 destroyKeyboard`（不清理 `CInputManager::m_pressed` 中该设备贡献的键码，也不回滚 share 状态）。
- 攻击路径：恶意客户端 bind vkbd → `set_key(key=K, pressed=1)` 后永久保持。此后：用户对 K 的 release 被 `ANYPRESSED` 吞（InputManager.cpp:1712-1714 逻辑），K 在 seat `m_pressed` 中滞留 → 后续每次 K 的 press 命中 `CONTAINS && pressed → return` 被吞。K 对全体焦点客户端永久失效（键绑定链仍可用，但用户无法输入该字符）。攻击者同时持多个 vkbd 可禁掉全键盘字母区，实现"无法打字"的持久 DoS；锁屏场景可让用户连密码都打不出来（配合 F-PR2-02 是双重锁屏拒服）。物理侧对应：持键拔出键盘同样残留 seat `m_pressed`（每次重插后至少丢一次按键，形成可复现的丢键竞态）。
- PoC 思路（文字）：脚本化 vkbd 客户端持下 `a` 键；用 wtype/xdotool 输入含 `a` 的字符串，验证 `a` 全部丢失且重启输入无效、仅销毁攻击连接后恢复（攻击者可重连再锁）。ASAN 无关，逻辑漏洞。
- 修复建议：share 状态仅统计 `isTrusted/物理` 设备或经 F-INP-01 的门控；设备销毁路径（`destroyKeyboard`）同步清除其在全局/共享结构中的痕迹（seat `m_pressed`、per-kbd `m_pressed`、`CInputState`——后者 onDeviceRemoved 已做）；`release_pressed_on_close` 默认改为 true 且销毁时始终向 seat 合成 release。

---

## F-INP-06（Medium）— virtual-pointer 无门控：客户端任意移动/点击光标 → 触发 mousebind、点击锁屏期权限弹窗、CLICKMODE_KILL 联动杀窗口

- 位置：
  - `src/protocols/VirtualPointer.cpp:18-62`（`setMotion/setMotionAbsolute/setButton` 全信任客户端，含 `button` 任意枚举值 `sc<wl_pointer_button_state>(state)` 不校验；`src/managers/input/InputManager.cpp:1279-1285 newVirtualMouse` 直接注册进全局指针链）
  - `src/pointer/PointerManager.cpp:1124-1127`（button → `onMouseButton`）→ `src/managers/input/InputManager.cpp:719-753`：`Keybinds::mgr()->onMouseEvent`（mouse:N 键绑定，可绑任意 dispatcher；锁屏期由 canInvokeNow 拦，未锁定则放行）、`processMouseDownNormal`（856/861 行锁屏分支）、以及 `CLICKMODE_KILL` 下 `processMouseDownKill`（InputManager.cpp:923-948：直接 `kill(pid, SIGKILL)`，**无 isSessionLocked 检查**）。
- 攻击路径：
  1. 未锁定时：客户端把光标 warp 到目标窗口，发 button 事件触发用户绑定的 mousebind（含 exec 语义即回到 F-INP-01 的 RCE 面）；或纯粹模拟点击批准恰好弹出的权限对话框（截屏/键盘等 PERMISSION 对话框以 FOCUS_PRIORITY 参与 refocus，`InputManager.cpp:333-338`，攻击者可用 vkbd+虚拟指针在锁定/解锁边界期自动点击"允许"——权限系统被自动化钓鱼）。
  2. 锁定期间：若用户启用 killmode（`binds --kill` 或 SUPER+ESC 进入），恶意 vkbd 发 Escape 复位、攻击者虚拟指针点击可杀任意几何位置窗口（`processMouseDownKill` 不查锁屏；虽需 killmode 先被开启，但进入 killmode 的 Escape 切换本身也可被注入，见 `Manager.cpp:916+`）。
- PoC 思路（文字）：zwlr_virtual_pointer_v1 绑 seat 全局 → motion_absolute 指向权限对话框按钮坐标（可用窗口几何从 IPC 推断）→ button(BTN_LEFT) down/up + frame → 验证对话框被点击。killmode 变体：管理员配置演示 SUPER+ESC 后，锁屏+虚拟点击杀窗口。
- 修复建议：与 F-INP-01 同法给 virtual-pointer 增加权限门（默认弹窗而非静默 ALLOW）；`processMouseDownKill` 与 `setClickMode(CLICKMODE_KILL)` 增加 `isSessionLocked()` 守卫；`setButton` 校验 `state ∈ {0,1}` 并拒绝非 BTN_LEFT/RIGHT/MIDDLE/侧键集合外的伪造值（对齐 libinput 语义）。

---

## F-INP-07（Low）— CBind::invoke 的 requestRelease 递归与鼠标设备销毁时 `invokeReleaseCallbacks` 重入 `m_inputState`

- 位置：`src/keybinds/Manager.cpp:610-616`（`!INPUT_KEY && pressed && followUp==TRIGGER_RELEASE → invokeBind(bind, false)` 自递归；深度靠 `m_bindInvocationDepth` 记录但不设上限）、`Manager.cpp:624-639`（`invokeReleaseCallbacks` 循环中 `m_inputState.find(...)`，若回调再次触发 `onDeviceRemoved`/`clearBinds` 会使 vector 重分配，`release.device.lock()` 结果迭代悬置——当前实现每次 find 重查，风险限于 `releases` 快照内的 `release.bind` 被并发 disable 的 TOCTOU）。
- 攻击路径：客户端触发（vkbd/virtual pointer）一个回调中再触发同键绑定的 Lua 绑定（requestRelease 自反）→ 无深度上限的互递归 → 栈耗尽崩溃（配合 F-INP-02 时机可选在锁屏期）。属可达性依赖配置写法，评为 Low。
- 修复建议：`m_bindInvocationDepth > N(如 16)` 时拒绝再入并记错误；`invokeReleaseCallbacks` 前对 `bind->enabled()` 复查。

---

## F-INP-08（Low）— CVirtualKeyboardV1Resource::destroy 双通道触发：destroy 事件重复发射、destroyKeyboard 重入

- 位置：`src/protocols/VirtualKeyboard.cpp:38-39`（`setDestroy` 与 `setOnDestroy` 都调 `destroy()`）；`destroy()` 内 `m_events.destroy.emit()` 两次（客户端显式 destroy 后资源析构再发一次）→ 上层 `CVirtualKeyboard`（`src/devices/VirtualKeyboard.cpp:18-21`）两次 `m_events.destroy.emit` → `setupKeyboard` 静态监听器两次 `destroyKeyboard`（第二次 erase 空转，但 `Keybinds::mgr()->onDeviceRemoved` 与 `removeFromHIDs/updateCapabilities` 重复执行，且 release 回调会被同一设备触发两轮）。
- PoC：客户端对同一 vkbd 先显式 `destroy()` 再关连接；ASAN/日志可观察到双销毁链；与 F-INP-04 的空指针问题叠加时崩溃概率上升。
- 修复建议：`destroy()` 幂等化（`m_destroyed` 标志）；`setOnDestroy` 只保留资源释放，不再走 `destroy()` 分支。

---

## F-INP-09（Low）— Keybinds::CResolver 键码缓存以裸指针为键，热插拔指针复用返回过期 keycode；resolveKeycode 数字解析未防溢出

- 位置：`src/keybinds/Resolver.cpp:44-89`
  - 缓存键 `std::format("{}{}", rc<uintptr_t>(KB.get()), key)` —— 键盘对象销毁后地址被新设备复用（热插拔循环极易复用同一 slab 地址）→ 旧键名命中过期 keycode（不同 keymap 布局下解析到错误键）→ 绑定匹配异常（用户可见的"按 A 触发 B 绑定"）。缓存仅在 seat 键盘 keymap 事件时清理（InputManager.cpp:1162-1173），`setKeyboard` 换设备不清理。
  - `std::stoi(key)`（48-50 行）对超长数字串抛 `std::out_of_range`，配置解析线程未捕获 → 合成器 abort（需攻击者能改配置，评 Low；若未来开放客户端提交绑定字符串则升 High）。
- 修复建议：缓存随 `destroyKeyboard/setKeyboard` 失效（或键改为设备序列号+generation）；数字解析用 `std::from_chars` 并检查错误码。

---

## F-INP-10（Info/Low）— workspace 规则 `onCreatedEmptyRunCmd` 可被客户端命名 ext-workspace 触发

- 位置：`src/workspace/HLWorkspace.cpp:93-96`（空工作区创建即执行规则命令）。工作区名可来自 ext-workspace v1 客户端 `create_workspace(name)`；若用户按名配置了 on_created_empty_run_cmd，攻击者可猜测/枚举名称在其自有空工作区上触发该配置的 exec（时机受攻击者控制，命令本身来自配置）。
- 修复建议：`onCreatedEmptyRunCmd` 仅在"合成器自身创建且非客户端命名"的工作区上执行。

---

## 统计与建议优先级

| ID | 严重度 | 一句话 |
|---|---|---|
| F-INP-01 | Critical | vkbd 任意客户端触发键绑定 → exec RCE；keyboard 权限默认 ALLOW 且按名记忆 |
| F-INP-02 | High | release 回调链绕过 canInvokeNow → 锁屏期执行绑定（销毁设备即扳机） |
| F-INP-03 | High | TouchDevice 销毁顺序错误 → 拔出竞态 UAF 写 |
| F-INP-05 | High | vkbd 持键不释放 → 永久致盲任意按键（DoS，锁屏可用） |
| F-INP-04 | Medium | 键盘事件监听器缺空判 + 裸 this/keeb 捕获 |
| F-INP-06 | Medium | virtual-pointer 无门控：注入点击/触发 mousebind/杀窗口路径 |
| F-INP-07 | Low | requestRelease 自递归无深度上限 |
| F-INP-08 | Low | vkbd destroy 双发射，销毁链重入 |
| F-INP-09 | Low | Resolver 裸指针缓存过期 + stoi 溢出 |
| F-INP-10 | Low/Info | 客户端命名工作区触发配置的 exec |

系统性建议：
1. 输入域应引入"受信设备"概念（物理 libinput = 受信；protocol 虚拟设备 = 需授权），当前 vkbd/vpointer 与物理设备在键绑定/mousebind 执行面上完全同权。
2. 所有"绕过 canInvokeNow"的旁路（invokeReleaseCallbacks、session-inactive flush）统一收敛到同一个 `executeBindIfAllowed()` 包装。
3. 设备销毁统一改为 deferred-erase（事件循环任务），监听器一律 WP 捕获 + 判空，杜绝 emit 内析构。
4. 锁屏进入点补 `CInputState`/seat `m_pressed`/vkbd 分享态的合成清理（释放而非丢弃，避免卡键）。

（本文与 07-protocols-misc.md 的 F-PR2-01/02 存在链路上互证：keymap 崩溃与 IME 劫持由彼处覆盖，键绑定执行链与设备生命周期由本文覆盖。）
