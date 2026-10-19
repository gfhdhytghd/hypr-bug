# 16 — 插件运行时 API 与钩子系统 (F-HOOK)

仓库：Hyprland fork @ `e38076ac9`（已并入 REV-06..14）。审计域：`src/plugins/`（HookSystem / PluginAPI / PluginSystem）、hyprutils Signal 使用点、EventBus emit 路径、插件加载/卸载生命周期。
威胁模型：恶意或有 bug 的插件 `.so`（经 F-IPC-03 的任意路径 load 即现实）；普通 Wayland/X 客户端可触发的含插件回调路径；任意 IPC 客户端（`hyprctl plugin …`）。

背景说明：本 fork 已移除旧 `g_pHookSystem`/`CSignalHook` 动态回调系统（`PluginSystem.cpp:149-152`、`PluginAPI.cpp:44-45,55-56` 的清理代码被注释），官方迁移方向是插件直接使用 `Event::bus()` + hyprutils `CSignalT`（`PluginAPI.hpp` registerCallbackDynamic 文档注释明示 "use Event::bus()"）。hyprutils 0.14.2 的 `CSignalBase::emitInternal`（`/usr/include/hyprutils/signal/Signal.hpp:15-23`，实现在 hyprutils `src/signal/Signal.cpp`）对 `m_vListeners` 做了快照复制、对 `m_vStaticListeners` 按值拷贝后调用 —— **emit 期间容器修改本身是安全的**；问题集中在**所有权与生命周期**：合成器对插件注入的回调没有任何台账。

---

## F-HOOK-01 ｜插件注册的 Signal 监听器（尤其 listenStatic）在卸载后无清理，emit 时跳入已 dlclose 的文本段 — 高危

**位置**
- `src/plugins/PluginSystem.cpp:139-190`（`unloadPlugin` 全文，无任何 listener 清理；149-152 是被注释掉的旧回调清理循环）
- `src/plugins/PluginSystem.hpp:27-32`（`CPlugin` 无 listener/回调台账字段；`m_registeredCallbacks` 已注释）
- `src/plugins/PluginAPI.cpp:38-47`（`registerCallbackDynamic` 返回 nullptr 的 stub）与 `PluginAPI.hpp` 文档 "use Event::bus()"
- `src/event/EventBus.hpp:45-210`（`Event::bus()` 单例及其全部长生命周期信号）
- hyprutils `Listener.hpp:21-26` / `Signal.hpp:21-22`：`listenStatic` 存入 `m_vStaticListeners`（`SP`），**只有信号宿主对象析构才会释放**；`listen` 返回 SP，若插件侧泄漏（不释放）则 WP 永不过期。

**攻击路径**
1. 插件（恶意或仅仅是忘记解绑的正常插件）在 init 中执行 `Event::bus()->m_events.window.open.listenStatic([...]{ plugin 代码; })`，或 `g_pCompositor`/`Event::bus()` 级别任意 `CSignalT::listen(...)` 后把返回 SP 存进未释放的堆/静态对象。
2. `HyprlandAPI` 没有 `removeListener`/按 handle 批量断开 API，`unloadPlugin` 也不遍历任何台账（因为根本没有）→ `dlclose`（`PluginSystem.cpp:184`）unmap 插件文本段。
3. `m_vStaticListeners` 里的 `std::function` 目标代码位于已 unmap 区域；下一次任意 `emit`（`window.open` 由任何客户端开窗触发，`input.*` 由指针/键盘事件触发，`tick` 每帧触发）→ `emitInternal` → `l->emitInternal(args)` 跳入未映射地址 → SIGSEGV → `handleUnrecoverableSignal` → 整个会话崩溃。
   对 `listen`（非 static）同样成立：dlclose 时插件的静态对象析构会正常释放 SP，但堆上泄漏/故意保留的 listener 控制块不会过期（WP 不 expired），emit 照样调用悬挂 handler；且 dynamic listener 的 `std::function` 的 invoker 代码也在 `.so` 内。
4. `removeAllHooksFrom` 只清理函数钩子（`PluginSystem.cpp:158`），与 signal listener 无关。

**PoC 思路**（文字）：一个最小插件在 `pluginInit` 中对 `Event::bus()->m_events.input.pointer…`/`window.open` 调 `listenStatic`，闭包体为空即可；`hyprctl plugin load /tmp/x.so` 成功后 `hyprctl plugin unload /tmp/x.so`（unload 无权限门，见 F-HOOK-05），随后随便开一个窗口或移动鼠标 → 合成器 SIGSEGV。恶意插件还可把 static listener 注册到 `tick`/render 事件，保证卸载后 16ms 内必炸，形成"延迟必炸"DoS，且触发权在普通客户端。

**修复建议**：为 `CPlugin` 增加台账：API 层提供受跟踪的 `HyprlandAPI::listen(handle, signal, fn)` 包装（返回带 handle 归属的 RAII 句柄），`unloadPlugin` 在 `dlclose` 前统一 reset/断开；hyprutils 侧为 `CSignalBase` 增加按 owner 移除 static listener 的能力（或引入 owner-tag）。至少：`listenStatic` 类路径应记录并在 `unloadPlugin` 中强制失效；文档禁止插件对 `Event::bus()` 级信号使用 `listenStatic`。

---

## F-HOOK-02 ｜加载时只比对 "0.2" 版本字符串，从不比对 API hash → ABI 漂移下加载插件 = 类型混淆内存破坏 — 高危

**位置**
- `src/plugins/PluginSystem.cpp:88-105`：仅 dlsym `__hyprland_api_get_version` 并比较 `HYPRLAND_API_VERSION`（`PluginAPI.hpp:21` 固定 `"0.2"`）。
- `src/plugins/PluginAPI.hpp:392-411`：`__hyprland_api_get_hash()` / `__hyprland_api_get_client_hash()` 定义了含 `GIT_COMMIT_HASH`、hyprutils/graphics/cursor/lang 版本的复合 hash，但**全仓库无任何加载期比对**（grep 仅 `SystemInfo.cpp:83,127` 用于版本展示）。

**攻击路径**
1. `HYPRLAND_API_VERSION` 是长期不变的常量；任何为该 fork 的*其他 commit*（或不同 hyprutils/hyprgraphics 小版本）编译的 `.so` 都能通过 `PluginSystem.cpp:100` 检查被 `dlopen`。
2. 插件按旧布局构造/持有合成器导出类型的实例（`IPC::Socket1::SCommand`、`Event::CCustomEvent`、`Layout::ITiledAlgorithm`、`Config::Values::IValue`、`IHyprWindowDecoration` 等含虚表/跨 ABI 传递 `SP<>` 的结构）→ 字段错位、vtable 偏移错误 → 任意读写/控制流劫持，且这一切发生在"普通、无恶意"的旧插件上。
3. 与 F-IPC-03 的任意路径 load 复合：攻击者只需在其 `.so` 里导出 `__hyprland_api_get_version` 返回 `"0.2"`（内联宏展开自动如此），不需要与当前 commit 匹配；hash 防线形同虚设。

**PoC 思路**：用旧 commit 头文件编译任一已知插件（例如 layout 插件），对新编译的基线 `hyprctl plugin load` → 观察 `addTiledAlgo`/回调签名处的字段错位崩溃或更隐蔽的越界写；证明 hash 不匹配也照样过闸。

**修复建议**：`loadPluginInternal` 中 dlsym `__hyprland_api_get_client_hash` 并与 `__hyprland_api_get_hash()` 严格相等比较，不匹配拒绝加载（提供显式逃生阀 env）；同时把 `apiVerFunc()` 返回值改为跨 ABI 安全的 `const char*`。

---

## F-HOOK-03 ｜插件布局算法对象在卸载后滞留：monitor-less workspace 跳过清理 → 虚表悬挂；注册失败仍记录名字 → 卸载可摘除他人算法 — 高危

**位置**
- `src/layout/supplementary/WorkspaceAlgoMatcher.cpp:115-144`（`updateWorkspaceLayouts`；**122-123** 的 guard `if (!WORKSPACE || !WORKSPACE->m_monitor || !WORKSPACE->space()) continue;`）
- `src/layout/supplementary/WorkspaceAlgoMatcher.cpp:74-90`（`unregisterAlgo` 依赖 `updateWorkspaceLayouts` 来"销毁仍存活的插件 UP"——见其 85-86 行注释，但该替换对跳过的 workspace 不发生）
- `src/layout/supplementary/WorkspaceAlgoMatcher.cpp:92-95`（`algoForNameTiled` 调用工厂 std::function，其 invoker 代码在插件 `.so` 内）
- `src/plugins/PluginAPI.cpp:76-96`：`addTiledAlgo`/`addFloatingAlgo` 先 `PLUGIN->m_registeredAlgos.emplace_back(name)`（82、93 行）**再**调用 register（84/95 行），注册失败（重名）名字仍被记录；
- `src/plugins/PluginAPI.cpp:98-107`：`removeAlgo` 不校验 `name` 归属，任何插件可按名字摘除其他插件的算法。
- `src/plugins/PluginSystem.cpp:154-156`：卸载按 `m_registeredAlgos` 逐个 `unregisterAlgo`。

**攻击路径 A（悬挂 vtable，普通插件+普通硬件事件）**
1. 插件 P 注册 tiled algo "px"，`general:layout=px` 或 workspace rule 使其实例被 `CAlgorithm` 的 `UP<ITiledAlgorithm>` 持有。P 的派生类构造/析构器与全部虚函数都在 P 的 `.so`。
2. 拔掉该 workspace 所在显示器（或处于 reconfigure 瞬态，`m_monitor==null`）→ workspace 滞留。
3. `hyprctl plugin unload P.so` → `unregisterAlgo("px")` → `updateWorkspaceLayouts` 对该 workspace **在 122 行 continue**，插件算法实例未被换出。
4. `dlclose`。之后：(a) 再插显示器/任何触发 `updateWorkspaceLayouts` 的路径走到 137 行 `getNameForTiledAlgo(TILED_ALGO.get())` → `algo->layoutName()` 虚调用进未映射文本 → SIGSEGV；(b) workspace 最终销毁（客户端关闭其最后窗口、`State::Workspace::clear`）→ `~CAlgorithm` 释放 `UP<ITiledAlgorithm>` → 插件内虚析构 → UAF-execute。
115 行上方注释"Workspaces can briefly outlive usable layout state"恰说明此状态真实可达。

**攻击路径 B（跨插件篡改/DoS）**：插件 A、B 都注册名字 `foo`（B 成功、A 失败返回 false，但 A 的 `m_registeredAlgos` 已含 `foo`）→ 卸载 A 时 `unregisterAlgo("foo")` 摘除 **B** 的算法并把 B 的活跃实例替换为默认 → B 的布局静默失效、且若 B 实例处于路径 A 的滞留状态同样悬挂。`removeAlgo` 无归属校验则允许任何已加载插件随时做同样的事。

**PoC 思路**：插件注册 tiled algo 并设为默认布局；把某 workspace 与其显示器一起置于断连状态；`plugin unload`；重连显示器 → 崩溃。第二个 PoC：两个插件重名注册，卸载失败方 → 正常方布局被换回 dwindle。

**修复建议**：`updateWorkspaceLayouts` 对跳过的 workspace 也要在其算法属于被注销名字时立即 `updateTiledAlgo(默认)` 或标记延迟销毁；`addTiledAlgo` 仅在注册成功后记录名字；`removeAlgo`/`unregisterAlgo` 校验名字归属（matcher 记录 name→handle）；`unregisterAlgo` 同时清理 `m_algoNames` 中指向插件 `.so` 内 `type_info` 对象的键（该 key 在 dlclose 后悬挂，147-160 的查表按指针比较尚可，但 `typeid(*algo)` 求值本身对死对象做虚调用）。

---

## F-HOOK-04 ｜卸载时装饰清理只覆盖 windows() 列表：正在淡出/销毁中的窗口漏网 → 装饰对象悬挂 vtable — 高危（客户端可触发）

**位置**
- `src/plugins/PluginSystem.cpp:160-162`（卸载循环调用 `removeWindowDecoration`）
- `src/plugins/PluginAPI.cpp:159-175`（`removeWindowDecoration` 仅遍历 `Desktop::windowState()->windows()`）
- `src/desktop/state/WindowState.cpp:23-25`（`removeSafe` 按指针 erase）与 `src/desktop/view/window/Window.cpp:1825`（**销毁开始**即从 windows() 摘除，此时 CWindow/presentation 因淡出动画仍存活）
- `src/desktop/view/window/WindowPresentation.hpp:104`（`m_windowDecorations` 持 `SP<IHyprWindowDecoration>`）
- `src/plugins/PluginSystem.hpp:27`（`m_registeredDecorations` 为永不剪枝的裸指针表）。

**攻击路径**
1. 插件为窗口 W 添加装饰（装饰派生类在插件 `.so`，虚析构/`drawOn`/`isActive` 等虚函数均在那里）。
2. 客户端关闭 W → `CWindow::onDestroy` 于 `Window.cpp:1825` 调 `removeSafe` → W 已不在 `windows()`，但淡出期间 presentation 仍持有装饰的最后一个 `SP`。
3. 竞态窗口内（默认淡出动画数百 ms，动画时长可配置）任意 IPC 客户端执行 `hyprctl plugin unload <path>`（无权限门，见 F-HOOK-05）→ 卸载循环遍历 `windows()` **找不到 W** → 装饰不被移除 → `dlclose`。
4. 淡出结束/下一帧渲染销毁 presentation → 释放装饰 `SP` → 虚析构跳进已 unmap 的 `.so` → SIGSEGV；若淡出 pass 仍遍历 `decorations()`（Renderer 装饰循环）则更早。
5. 另外 `m_registeredDecorations` 里的裸指针在窗口先亡后成为悬垂值：卸载时 `removeWindowDecoration` 用 `d.get() == pDecoration` 纯比较不会解引用，但**堆地址复用**后可能与别的窗口上*活着的*装饰指针相同 → 误删他人装饰（完整性/DoS）。

**PoC 思路**：插件对所有开窗加装饰；脚本 `对某窗口发送 close 请求，随后立即 hyprctl plugin unload`，用淡出时长放大竞态 → 必现崩溃。对照组：不卸载则无问题，证明是卸载路径缺陷。

**修复建议**：装饰台账改由 presentation 侧携带 owner handle，卸载时按 owner 全量清除（遍历 windows + fadingOut/other-view 状态，或维护全局装饰注册表）；`CPlugin::m_registeredDecorations` 改 `WP` 并在窗口销毁事件剪枝；同时把插件装饰与 F-HOOK-01 的 listener 台账一起在 `dlclose` 前强制析构（析构发生在插件文本段仍在映射时）。

---

## F-HOOK-05 ｜插件卸载/加载与运行中回调的竞态：同步 `plugin unload` 无权限、可在插件栈帧内 dlclose；deferred promise / doLater / 定时器回调不在卸载清理范围 — 高危

**位置**
- `src/ipc/s1/Commands.cpp:1770-1805`（`dispatchPlugin`：`unload` 分支同步 `unloadPlugin`，**无 DynamicPermissionManager 门**；`load` 才有）
- `src/ipc/s1/S1.cpp:85-113`（`dispatchSingle` 持 `SP<SCommand> matched` 后调用 `matched->handler`）与 `S1.cpp:217-222`（`invoke` **同步** dispatch）
- `src/plugins/PluginAPI.cpp:61-66`（`invokeHyprctlCommand` → `sock()->invoke(...)`，可在插件回调内重入 dispatch）
- `src/plugins/PluginSystem.cpp:16-64`（`loadPlugin` 捕获裸 `this` 与 resolver 的 promise 链；31 行 `then` 捕获 `this`）
- `src/plugins/PluginSystem.cpp:17-190`（`unloadPlugin` 不触碰事件循环队列/定时器/在途 promise）。

**攻击路径**
1. **自卸载重入**：插件 A 注册的 hyprctl 命令处理器（`registerHyprCtlCommand`，`PluginAPI.cpp:351-360`）在其 handler 内调用 `HyprlandAPI::invokeHyprctlCommand("plugin", "unload /path/A.so")` → 同步 `CSocket1::invoke` → `dispatchPlugin` → `unloadPlugin(A)` → `dlclose` 时 A 的 handler 栈帧仍在 A 的文本段 → 返回未映射地址 → 崩溃。一个有 bug 的插件即可触发；恶意插件可用它炸掉自己再配合 F-HOOK-01 炸别人。
2. **在途异步回调**：插件命令处理器返回 `SP<CPromise<std::string>>`（`SResponse` 支持 deferred，`S1.cpp` 批量聚合 155-205 行持有 responses），`.then` 闭包/`resolver` 对象的代码与 vtable 在插件 `.so`。任何客户端在 resolve 之前发 `hyprctl plugin unload <path>`（不需要任何权限）→ `dlclose` → resolve 时 `state->resolver->resolve(...)` 链上执行已 unmap 的闭包 → UAF。
3. **无权限 unload 本身**：`dispatchPlugin` unload 分支直接 `getPluginByPath + unloadPlugin`。任意本地 IPC 客户端都能远程"拔插件"，是 F-HOOK-01/03/04 客户端触发链的放大器（load 有 permission 门，unload 反而没有）。
4. **shutdown 竞态**：权限对话框挂起期间退出会话 → `g_pPluginSystem.reset()`（`Compositor.cpp:619`）后 promise `then` 仍持裸 `this` → resolve 时 UAF；`unloadPlugin` 的 `doLater` reload（`PluginSystem.cpp:189`）在拆毁期触发时 `Config::mgr()` 无 null 守卫（对比 174 行有）。

**PoC 思路**：插件命令 handler 内同步 unload 自身（路径 1）；或 handler 返回 promise、由 200ms 后的 timer resolve，测试脚本在 100ms 处 `hyprctl plugin unload`（路径 2）。

**修复建议**：`unload` 加与 load 同级的权限/来源校验；卸载改为"标记卸载 + doLater 实际 dlclose"，期间丢弃该插件所有在途 promise 续延（CPromise 增加按 owner 作废 API）；`invokeHyprctlCommand` 内部拒绝或延迟处理 `plugin unload` 自身（递归深度哨兵）；对 `loadPlugin` promise 链改用 WP/self 生命周期守卫，`g_pPluginSystem` 改 `SP`。

---

## F-HOOK-06 ｜插件 init 崩溃围栏是死代码：setjmp 无配对 longjmp，"crashed in init. Unloading." 永不可达 — 中危

**位置**
- `src/plugins/PluginSystem.cpp:109-122`（`setjmp(m_pluginFaultJumpBuf)` + else 分支"received a fatal signal" + catch 中 `unloadPlugin(PLUGIN, true)`）
- `src/plugins/PluginSystem.hpp:54`；全仓库 grep `longjmp`：除该行 `setjmp` 外**零命中** —— 没有任何信号处理器会 longjmp 回来。
- `src/Compositor.cpp:105-120, 296-297`：SIGSEGV/SIGABRT → `handleUnrecoverableSignal` → CrashReporter + 进程终止。

**攻击路径 / 影响**：插件 init 里的空指针/除零（普通 bug，或为 ABI 漂移 F-HOOK-02 的必然产物）→ SIGSEGV 直达 unrecoverable handler → **整个合成器连同所有会话死亡**；设计意图（丢插件、保留会话、eject 清理已注册资源）完全落空。反向看，如果未来接线 longjmp：从信号帧 longjmp 回 `setjmp` 会跳过 `initFunc` 中途获取的锁/半初始化状态（glibc 内部态、部分注册的 hook/装饰/lua 函数），`m_allowConfigVars=true`（111 行）在异常路径虽有 catch 复位、但 longjmp 不走 catch —— 围栏本身修复前也是危险的。

**PoC 思路**：一个在 init 里解引用 null 的插件，`hyprctl plugin load` → 观察会话整体崩溃 + crash dump，而非日志中的 "crashed in init. Unloading."。

**修复建议**：要么真正接线（专用信号处理器判断 PC 落在 dlopen 区间/线程局部标志后 `longjmp`，并让 eject 清理容忍半初始化；`sigsetjmp/siglongjmp` + 恢复掩码），要么删除假围栏并在文档中明确"加载未受信插件 = 全进程信任决定"。

---

## F-HOOK-07 ｜内联函数钩子（CFunctionHook）完整性缺陷：跨页 unhook 必炸、launch trampoline rel32 无范围校验、重叠 patch、跨插件悬挂 hook — 中危

**位置**（`src/plugins/HookSystem.cpp`）
- 跨页缺陷：hook 时 `PROTSTART/PROTLEN` 可覆盖两个页并整段 patch（220-227 行）；`unhook` 只对**一个页** mprotect RW（261 行）后 `memcpy(m_source, …, m_hookLen)`（264 行）写回 ORIGSIZE 字节 —— 目标函数尾部跨页时第二页仍是 R+X → memcpy SIGSEGV。267 行同样只恢复一页。
- rel32 无校验：185 行只对 `m_landTrampolineAddr` 做 2GB 检查；230-233 行写入 source→launch 的 rel32 **不做截断检查**；而 `getAddressForTrampo`（353-408 行）的 mmap 只是**hint**（无 MAP_FIXED/MAP_FIXED_NOREPLACE），attempt 1 接受任意返回地址（387-394 行）→ launch 距 source >2GB 时跳板指令被静默截断 → 控制流跳到错误地址。
- 重叠 patch：151-155 行去重仅比较 `source` 精确地址；两个函数入口的 patch 区域（prologue，最长 ~15+ 字节）可重叠（热联/同页紧排函数），`m_originalBytes` 快照彼此包含对方的 jmp，unhook 顺序不同即残留 jmp 指向已回收的 trampoline 页 → 后续调用执行已释放/复用页内容（可被堆/页喷复用的控制流劫持原语）。
- 跨插件悬挂：A 用 `createFunctionHook`（`PluginAPI.cpp:125-132`，source/destination 不做任何映射域校验；`getFunctionAddressFromSignature`/`findFunctionsByName` 用 `dlsym(nullptr,…)` 可见其他插件导出符号）把**插件 B 的函数**当 source、把 B 的另一个函数当 destination；B 先被无权限 unload（F-HOOK-05）→ A 的 hook 仍 active，land trampoline 回跳 `source+probe.len`（209 行）指向已 unmap → 任何一次原函数调用即崩；A 之后 `unhook` 对 unmapped `m_source` mprotect（失败未检查返回值，261/267 行）+ memcpy（264 行）→ 必炸。`removeAllHooksFrom`（`PluginSystem.cpp:158`）只清自己 owner 的。

**PoC 思路**：(a) 选一个入口距页尾 <5 字节的导出函数 hook 后 unload → 崩溃在 unhook；(b) 两个插件互 hook，先卸载被 hook 方 → 客户端正常操作触发崩溃；(c) 反复加载/卸载插件使 trampoline 页回收，再制造重叠 patch → 展示残留 jmp。

**修复建议**：unhook 按 hook 时的 PROTSTART/PROTLEN 保存并恢复（校验 mprotect 返回值）；对 launch trampoline 与所有 rel32 回填做 ±2GB 断言，越界改走绝对跳板或拒绝；patch 区间做 [start,end) 重叠检查而非地址精确匹配；禁止 source/destination 落在其他插件 `.so` 的映射内（用 dladdr 校验），或至少在卸载其他插件时联动使其相关 hook 失效。

---

## F-HOOK-08 ｜插件 Lua C 函数被用户脚本捕获后卸载：注册表移除不杀引用，pcall 跳入 dlclose 后的函数指针 — 中危

**位置**
- `src/plugins/PluginAPI.cpp:394-428`（`addLuaFunction` 原样把 `PLUGIN_LUA_FN` 裸函数指针交给 lua 管理器；`removeLuaFunction` 仅按 namespace+name 反注册）
- `src/config/lua/ConfigManager.cpp:1293-1320`（`SPluginLuaFunction{.fn = fn}` 裸指针）、`1322-1341`、`1344-1365`（`onPluginUnload` 按 handle 清 `m_pluginLuaFunctions` 并从当前 lua state 的 namespace 表删除条目）、`1461`（reload 时重注册存活函数）
- `src/plugins/PluginSystem.cpp:174-175`（`Config::mgr()->onPluginUnload`）与 `184`（`dlclose`）与 `189`（**dlclose 之后**才 doLater reload）。

**攻击路径**
1. 用户配置里 `local f = hl.plugin.myplug.do_thing`（或 `hl.keybind{…, f}` / `hl.submap` / timer 闭包捕获），Lua 把该 C 函数封成带 upvalue 的闭包，引用进入 registry/m_heldLuaRefs——`onPluginUnload` 只清空 namespace 表项，**无法触及已被用户脚本捕获的引用**。
2. `hyprctl plugin unload`：`onPluginUnload` 从 `m_pluginLuaFunctions` 抹除记录，随后 `dlclose`。
3. 之后任何触发该捕获引用的路径 —— keybind 按下、`callLuaFn` 派发的回调、hl timer、事件回调 —— `guardedPCall` 调用 `lua_CFunction` 指针 → 已 unmap → SIGSEGV。若 reload 因新配置引用变 nil 而报错则用户配置整段失效（可用性）。
4. 时序放大：真正的 `Config::mgr()->reload()` 排在 `dlclose` 之后（189 行），从 `dlclose` 到 reload 落地之间同一事件循环队列里派发的任何 lua 回调都在野指针上。

**PoC 思路**：插件注册一个 lua 函数；用户配置把它绑到某个 `hl.keybind`；`hyprctl plugin unload` 后按该键 → 崩溃。无需插件有恶意，正常"配置引用插件函数"的用法即触发。

**修复建议**：卸载前先在 lua state 中将该函数替换为报错占位（保持 registry 槽位，返回 "plugin unloaded"），并让依赖它的 keybind/timer 在 reload 完成前失效；或干脆要求带 lua C 函数的插件不可卸载（标记 `m_unloadable=false`）；把 reload 挪到 dlclose 之前执行并验证 state 中无残留引用。

---

## F-HOOK-09 ｜插件 API 普遍缺少归属校验与卸载台账：removeFunctionHook / removeEvent / removeAlgo 可跨插件操作；插件自定义事件永不随插件消亡 — 中危

**位置**
- `src/plugins/PluginAPI.cpp:134-141` + `src/plugins/HookSystem.cpp:284-287`（`removeFunctionHook` 只验证*调用者*是已加载插件，不校验 `hook->m_owner`；`removeHook` 恒返回 true）
- `src/plugins/PluginAPI.cpp:430-458` + `src/event/EventBus.cpp:23-37`（`addEvent` 不写入任何插件台账；`removeEvent` 按名字全表删除，无归属校验；`unloadPlugin` 不清理插件注册的 `CCustomEvent`）
- `src/plugins/PluginSystem.cpp:154-172`（卸载清理清单里没有 events）
- 对照 `src/plugins/PluginSystem.hpp:27-32`（台账字段缺 events）。

**攻击路径**
1. 恶意插件 A 启动时抢注事件名 `foo`（`try_emplace` 先到先得，`EventBus.cpp:23-31`），A 卸载后**名字永久占坑**：合法插件 B 永远注册不了 `foo`（持久性命名 DoS，跨会话重启才恢复）。
2. `removeEvent(handleB, "foo")` 不校验归属 → A 加载期间可逐个删除所有其他插件的自定义事件（篡改）。`removeAlgo` 同理（`PluginAPI.cpp:98-107`，见 F-HOOK-03 路径 B）。
3. A 注册的 CCustomEvent 的 `m_event` 信号上，B 的 `listen`/`listenStatic` 闭包继续存活；反之 A 自己留在该信号上的 static listener 与 F-HOOK-01 构成同样的悬挂执行链，而且此时事件对象还被 bus 强引用，连"宿主析构顺带清理"都指望不上。

**PoC 思路**：插件 A init 时注册 100 个常见命名（`myplugin_event` 等）后立即被卸载 → 插件 B 的 addEvent 全部失败；A 加载时调用 `removeEvent(自己的handle, B的事件名)` → B 功能失效。

**修复建议**：`CPlugin` 增 `m_registeredEvents` 台账，`unloadPlugin` 统一 `removePluginEvent`；`removeEvent`/`removeAlgo`/`removeFunctionHook` 校验对象归属（`handle` 与注册者一致）；`removeHook` 返回真实结果。

---

## F-HOOK-10 ｜死 API 面与误导性契约：registerCallbackDynamic 返回 nullptr、unregisterCallback 恒 true；hyprutils 弃用 typed/any 混用无防护 — 低危（信息性）

**位置**
- `src/plugins/PluginAPI.cpp:38-59`：`registerCallbackDynamic` 校验 handle 后返回 `nullptr`（46 行），`unregisterCallback` 校验 handle 后恒 `true`（58 行）。仍在用 v6 动态回调 API 的第三方插件会拿到"看似注册成功（unregister 返回 true）实则永不被调用"的静默失效；把 nullptr SP 当句柄保存再解引用则是插件侧崩溃 → 结合 F-HOOK-06（无崩溃围栏）炸掉整个合成器。
- `src/plugins/HookSystem.hpp:58-59` + `HookSystem.cpp:284-287`：`initHook/removeHook` 对外暴露裸 `CFunctionHook*`，所有权在 CHookSystem，插件误用（卸载后再调 `m_original`/重复 remove）无诊断。
- hyprutils `Signal.hpp:67-72,107-110`：`registerListener(std::function<void(std::any)>)` 与 `CSignal::emit(std::any)` 标记 deprecated 且在 `CSignalT` 上为 UB（`Listener.hpp:17`），插件头文件原样暴露这些入口，任何插件用旧式 `registerListener` 挂到 typed 信号即为 UB，无编译期拦截。

**PoC 思路**：v6 风格插件（仅 `registerCallbackDynamic` + `unregisterCallback`）加载后事件回调永不触发、接口却报成功；或 `SP` 句柄为空时插件直接 `fn->…`。

**修复建议**：stub 化应彻底——要么返回明确的失败并在文档标注移除，要么恢复实现并把清理接入 `CPlugin` 台账；`unregisterCallback` 不再恒真；考虑从插件可见头文件中剔除 `registerListener`/`CSignal::emit(std::any)` 重载（`[[deprecated]]` 升级为 `-Werror` 或从导出面删除）。

---

## 附注（查重与范围说明）

- emit-期间容器修改（本域传统关注点）在 hyprutils 0.14.2 已由快照复制正确处理（`emitInternal`，hyprutils `src/signal/Signal.cpp`），本 dossier 未立项；但 `const auto STATICS = m_vStaticListeners;` 每次 emit 全量拷贝 SP 向量 + 原子增减引用计数，在 render/tick 等热路径叠加插件监听器数量后是明显性能税（非安全问题，建议 wiki/上游跟进项）。
- `plugin load` 的权限门（`PluginSystem.cpp:21-57`）与 socket 层鉴权属 F-IPC/F-PERM 域；本域仅记录 `unload` 缺对称门（F-HOOK-05-3）作为触发放大器。
- EventLoop 定时器自身实现问题属 F-TMR 域；F-HOOK-05-2/-4 只覆盖"插件回调未纳入卸载台账"的接口缺口。
- config-lua 域（04 号 dossier）覆盖 lua 运行时；F-HOOK-08 仅针对插件 Lua C 函数的卸载生命周期。
- 已核对 `main @ e38076ac9`：REV 系列对 `m_activeHooks` 去重（HookSystem.cpp:151）、hyprctl 命令 SP 台账（m_registeredHyprctlCommands + lock）等修复均在位，未重复立项。
