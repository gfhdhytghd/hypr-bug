# 安全审计报告 10 — 桌面对象生命周期与布局

- 仓库：/home/wilf/data/Hyprland（Hyprland v0.56.0 fork，main @ 397cb0efb）
- 审计域：`src/desktop/`（Window/Group/WLSurface 销毁链、focus 历史、swallow、fullscreen 交互）、`src/layout/`（master/dwindle/scrolling/monocle、Space、target）
- 威胁模型：恶意 Wayland/X11 客户端 + 竞态（含 IPC dispatch 任意客户端可触发的路径）
- 关键类型事实：`PHLWINDOW = SP<CWindow>`，`PHLWINDOWREF = WP<CWindow>`；hyprutils 0.14.2 `WP::operator==` 比较 `impl_` 指针（expired WP ≠ null SP）；`WP::operator-> = get()`，expired 时返回 **nullptr**（解引用即 null-this 崩溃）；`WP::operator bool == valid()`（对象 **destroying 中仍为 true**，不等于 `!expired()`）。

---

## F-DSK-01（高危）destroyWindow 不解除 group 成员 → group 持 expired WP，后续裸解引用崩溃

- 位置：
  - `src/desktop/view/window/Window.cpp:1809-1824`（`destroyWindow()`：调用 `m_swallowing->onDestroy()`、`g_layoutManager->removeTarget(m_target)`、`windowState()->removeSafe()`，**但没有** `m_grouping->group()->remove(...)`）
  - 对照 `src/desktop/view/window/Window.cpp:1668-1669`（`unmapWindow()` 才做 group 移除）
  - `src/desktop/view/window/Window.cpp:1608-1611`（`unmapWindow()` 在 `!wlSurface()->exists() || !m_isMapped` 时**提前 return**，跳过 group 移除、layout target 移除）
  - 崩溃点：`src/desktop/view/Group.cpp:415-429`（`updateWindowVisibility()` 对 `m_windows` 中 WP 直接 `x->setInputBlocked(...)`、`x->presentation()...`，无 `lock()`/expired 检查）
  - 崩溃点：`src/desktop/view/Group.cpp:462-469`（`updateWorkspace()` 同样 `w->m_monitor = ...` 裸解引用）
  - 崩溃点：`src/desktop/view/window/Window.cpp:1064-1077`（`deactivateGroupMembers()` 循环 `w->backend().isX11()`，`w` 为 WP，无 guard；每次组内窗口获焦都会走，见 `FocusState.cpp:227-228`）

- 攻击路径：窗口销毁链的不对称是核心缺陷。SP 释放的唯一常规入口是 `windowState()->removeSafe()`（`destroyWindow`），而 group 成员表 `CGroup::m_windows` 是 WP 容器，仅由 `unmapWindow` 清理。任何让 destroy 事件到达但 unmap 流程被跳过/提前 return 的时序（`CWLSurfaceResource::destroy()` 仅在 `m_mapped` 时 emit unmap，`src/protocols/core/Compositor.cpp:306-314`；`~CWLSurfaceResource` 甚至只 emit destroy 不 emit unmap，Compositor.cpp:295-298），都会留下 expired WP。此后同组任一成员窗口被聚焦 → `deactivateGroupMembers()`/`updateWindowVisibility()` 对 expired WP 调 `operator->` 得 nullptr → null-this 调成员 → 合成器崩溃（本地 DoS）。
- PoC 思路：恶意客户端创建窗口 A、B 并组（`group` 规则或 IPC togglegroup），映射后在同一 `wl_display` 批次内对 A 触发 surface 销毁（destroy surface resource 与断开连接竞态组合，令资源销毁顺序使 surface 的 `m_mapped` 先假/事件序错乱，`unmapWindow` 走 1608 提前 return），随后释放 A 的 SP；聚焦 B → 崩溃。
- 修复：1) `destroyWindow()` 补 `if (m_grouping->group()) m_grouping->group()->remove(m_self.lock(), DIRECTION_DEFAULT, REMOVE_FROM_GROUP_REASON_UNMAP_WINDOW);`（幂等，remove 自身有未找到早退）；2) `unmapWindow()` 提前 return 分支也要解除 group 成员并 `removeTarget`；3) `CGroup` 所有遍历 `m_windows` 处先 `lock()` 判空并顺手 `std::erase_if` expired 项（提供 `gc()`）。

## F-DSK-02（高危）CGroup::destroy() 对 expired 成员无限循环挂死合成器

- 位置：`src/desktop/view/Group.cpp:71-85` 与 `Group.cpp:234-244`
- 描述：`destroy()` 循环 `while (!m_windows.empty()) remove(m_windows.at(0).lock());`。当队首 WP 已 expired，`lock()` 返回 null SP；`remove(nullptr)` 中 `m_windows.at(i) == w` 比较的是 `impl_`（expired WP 的 `impl_` 非空，null SP 的为空，永不相等）→ `idx` 未命中 → 第 243 行直接 return，不 erase → **死循环**。这是 F-DSK-01 留下尸体后的确定性放大：从崩溃升级为可等待触发的 hang。
- 攻击路径：同 F-DSK-01 制造 expired 成员；然后任何调用 `group()->destroy()` 的路径（`src/config/shared/actions/ConfigActions.cpp:978`，即 IPC/keybind `togglegroup` 解散组）即触发单核 100% 死循环，整个合成器事件循环冻结。
- PoC 思路：脚本经 `hyprctl dispatch` 反复建立/解散含"受害组"的组；或用 `keyword` 触发含 togglegroup 的 config action。
- 修复：`remove()` 入口 `if (!w) { /* 按 identity 找不到就清 expired */ }`；更简单：`destroy()` 改为 `std::erase_if(m_windows, ...)` 前先 `gc()` expired，且循环内用索引快照、检测"本轮未移除任何元素则 break"。

## F-DSK-03（中危）swallow 按 PPID 祖先链匹配且不局限 workspace → 恶意进程可吞掉终端窗口实施 UI 欺骗/输入导向

- 位置：`src/desktop/view/window/WindowSwallowController.cpp:47-94`（`findCandidate`：`for i<25 { currentPid = getPPIDof(currentPid); 收集 pid==祖先 的窗口 }`，第 64-70 行无 workspace/monitor 过滤）、`104-114`（`reserveCandidate` 选 group-slot 放置）、`134-149`（`activateGroupSlot` 调 `replaceMember` 让 swallower 顶替终端在组内的槽位）
- 描述：swallow 候选选择信任"祖先进程 pid 相同"。任何从终端派生的进程（孙子、重父化到 systemd 后 pid 链仍含终端、或 `daemon()` 前后竞态窗口内的恶意进程）都可作为 swallower 匹配到 `swallow_regex` 命中的终端窗口。命中后终端被 `setHidden(true)` 并从 space 摘除 target，攻击者窗口占据其布局槽位并作为新映射窗口获得焦点——用户在终端原位看到攻击者窗口，按键流向攻击者窗口（焦点合法但语义被劫持），构成凭据收割面。跨 workspace 吞窗 + `toggleStandalone` 第 204 行 `moveToWorkspace(SWALLOWER->m_workspace)` 还能把受害者终端强行拉到攻击者 workspace。
- PoC 思路：恶意客户端从终端 fork 启动，map 一个无边框窗口，其 title 不含 exception 正则；开启 `misc:enable_swallow` 即触发吞窗；配合 `group:groupbar` 关闭时视觉更难分辨。
- 缓解现状：`enable_swallow` 默认 0；但配置用户常见开启。
- 修复：1) 候选过滤加 `w->m_workspace == m_window.m_workspace && w->m_monitor == m_window.m_monitor`；2) 仅允许**直接子进程**（`getPPIDof(pid) == candidatePid`）而非 25 层祖先链；3) 被吞窗口保持可见性可审计（swallow 状态 IPC 事件已有 activewindow，建议暴露 hidden 列表）。

## F-DSK-04（中危）swallow restore/toggle 在 swallower 失效态下解引用 null m_workspace 或向销毁中 space 写 target

- 位置：
  - `WindowSwallowController.cpp:188-189`：`if (!SWALLOWEE->grouping().group() && !SWALLOWEE->windowTarget()->space() && SWALLOWEE->mapped()) newTarget(SWALLOWEE->windowTarget(), SWALLOWER->m_workspace->space())` — 仅判 SWALLOWEE mapped，未判 `SWALLOWER->m_workspace` 非空。`CWindow::onUnmap` 在 `Window.cpp:543` 处 `m_workspace.reset()`；swallower unmap（未 destroy）后 relation 仍可能 PENDING/ACTIVE（onUnmap 只在映射态 unmap 路径清理，`Window.cpp:1624` 的提前 return 路径不清），此时 `restoreStandalone`/`toggleStandalone`（`207-208` 同样问题）对 null SP 调 `->space()`。
  - `WindowSwallowController.cpp:204`：`SWALLOWEE->moveToWorkspace(SWALLOWER->m_workspace)`（moveToWorkspace 有 null guard，`Window.cpp:448-450`，但随后 207-208 继续用 null workspace 取 space）。
  - 析构链触发：`Window.cpp:165-166`（`~CWindow` → `m_swallowing->onDestroy()` → `onUnmap()` → `restoreAfterSwallower` 在 `309-313` 调 `newTarget(...)`/读 `SWALLOWER->m_workspace`——此时 this 正在析构，成员半销毁；workspace 移除流程中 space 可能已随 workspace 解体）。
- 攻击路径：恶意 swallower 自 unmap（`xdg_surface` 协议内合法手段）后再触发 swallow toggle（keybind/`hyprctl dispatch swapstackactive` 类路径或对方触发 incoming 侧 restore），或让 swallower 在 workspace 销毁竞态中析构 → null SP 解引用 → 崩溃；或向销毁中的 space 注入 target → 后续 `removeTarget`/recalc 对已释放对象操作（UAF 窗口）。
- PoC 思路：自动化脚本：A(恶意) 吞 B(终端) → A 调 unmap → 立刻经 IPC 触发需要 restore 的路径（如再次 map/destroy B）→ 断言合成器 SIGSEGV。
- 修复：所有 `SWALLOWER->m_workspace->space()` 前判 `SWALLOWER && SWALLOWER->mapped() && SWALLOWER->m_workspace`；`~CWindow` 的 `onDestroy` 降级为"只 clear relation、不做布局复活"（复活逻辑移到事件循环 `doLater` 并用 WP guard）。

## F-DSK-05（中危）后端事件监听器捕获裸 this，仅 destroyWindow 注销；SP 先亡则后续事件 UAF

- 位置：`src/desktop/view/window/Window.cpp:133-150`（14 个 `m_backend->m_events.*.listen([this]{...})`）；`Window.cpp:1819`（唯一注销点 `m_backendListeners = {}` 在 `destroyWindow` 内）
- 描述：CWindow 死亡但 `destroyWindow` 未运行的路径（例：workspace/monitor 拆卸直接 `windowState()->clear()`/`removeSafe` 释放最后一个 SP；X11 后端 SP 由 xwm 持有生命周期与 window 不同步）下，backend 对象若仍存活并 emit（恶意 X11 客户端可继续向 xwm 发送事件序列触发 UnmapNotify/ConfigureRequest 等），监听 lambda 对已释放 CWindow 调 `unmapWindow()/onConfigureRequest()` → UAF。Wayland 侧 `CWLSurface::init` 的 `[this]` 监听（`WLSurface.cpp:169`）同理依赖 `unassign/destroy` 被调用。
- PoC 思路：xprop/xcb 客户端针对被 workspace 移除竞态中的窗口持续 commit/configure；或让 compositor 触发 `removeSafe` 的调用序（workspace destroy IPC）与客户端 destroy 请求赛跑，用 ASan 验证。
- 修复：监听 lambda 统一 `[this, self = m_self] { if (!self) return; ... }`（注意用 `!self->…` 前先 lock 成活 SP），或 CWindow 析构函数中兜底 `m_backendListeners = {}`。

## F-DSK-06（中危）focus 历史容器对 WP 裸解引用 + 工作区历史 gc 逻辑缺陷致过期条目存活

- 位置：
  - `src/desktop/history/WindowHistoryTracker.cpp:36-44`：`historyForWorkspace` 循环内 `w->m_workspace != ws` 直接 `operator->`（expired→nullptr→读 nullptr+offsetof 崩溃）。`gc()` 只删 `!e`（`valid()`），对象处于 destroying 标志时 valid()==true 仍保留。调用方含 `ScrollingAlgorithm`/`MonocleAlgorithm.cpp:103`（history 反查）、`Window.cpp:1644-1656`（unmap 找组内下一个）。
  - `src/desktop/history/WorkspaceHistoryTracker.cpp:57-68`：`gc()` 中 `monitorCounts | std::views::drop(1)` 疑似 off-by-one——首个记录的 monitor 永不参与重复判定，其**已死亡 workspace 的条目永不被清除**（`return !entry.workspace` 分支对首 monitor 不可达），`previousWorkspace()` 可返回 `workspace` 已 expired 的条目，调用方 `entry.workspace->...` null 解引用风险。
- 攻击路径：恶意客户端高频 map/unmap 污染窗口历史 + 断开连接让窗口在"destroying"标志窗口期被读取（`unmapWindow` 的 history 遍历即读点，`Window.cpp:1644`）；工作区历史缺陷在"首个被记录 monitor 上销毁工作区"后触发 `workspace_back_and_forth` 类绑定 → 崩溃。
- PoC 思路：循环 spawn 崩溃式客户端（map 后立即 kill）200 次，随后触发 monocle/scrolling 历史反查路径与 `workspace back_and_forth`。
- 修复：history 容器读路径统一 `lock()` 判空；`gc()` 条件改为 `e.expired()`；修正 drop(1)（应比较 `entry.monitor` 与"已见集合"中除自身外条目，正确写法是直接查 set 再决定，不 drop）。

## F-DSK-07（中危）MasterAlgorithm 空/零分母：SIGFPE 与 NaN 几何进整数转换（UB）

- 位置：
  - `src/layout/algorithm/tiled/master/MasterAlgorithm.cpp:988-994`：`runOrientationCycle` 在 `cycle` 非空假设下做 `nextOrPrev % sc<int>(cycle.size())`——若 `buildOrientationCycleVectorFromVars(vars)` 全非法输入且 fallback 分支被 vars 非 nullptr 跳过（第 967-971 行 `vars != nullptr` 时不走 fallback 填充），空 cycle → **整数 mod 0 → SIGFPE 崩溃**；负向环绕 `cycle.size() + (nextOrPrev % size)` 对 `|nextOrPrev| >= 2*size` 越界后 `cycle.at()` 抛 `out_of_range`。任何 Wayland/X11 客户端都能 `hyprctl dispatch`（IPC 无鉴权）调用 master 布局的 orientation-cycle submap 参数路径。
  - `MasterAlgorithm.cpp:1078-1079`：`totalSize / MASTERS`、`totalSize / STACKWINDOWS` 无零 guard（`m:nmaster` 规则/`m/master_mitosis` 等组合使 STACKWINDOWS==0 时 inf；NaN 会沿 `1134/1171/1208/1238` 行 `WORKAREA.w / masterAccumulatedSize`（accumulated 可为 0，见智能缩放累加循环 1084-1092，center 跳过分支可使某一类累加和为 0）传播）。`percSize` 变 NaN → `setPositionGlobal(NaN)` → 下游 `sc<int>` NaN = UB → 巨尺寸 CBox → 渲染层巨额 damage/分配 DoS。
  - `MasterAlgorithm.cpp:261`：`1.f / (WINDOWSONWORKSPACE - 1)` 在窗口数为 1 时 inf，使 MAXSIZE 检查恒真 → 窗口被意外强制 floating（行为缺陷，配合 `std::erase(m_masterNodesData, PNODE)` 改变节点集）。
- PoC 思路：恶意页内嵌 iframe 类 X11 客户端经 IPC `dispatch masters:orientationcycle [非法vars]` 触发 SIGFPE；对 NaN 路径，用 `hyprctl setprop`/窗口规则把 nmaster 推到等于窗口数后快速 map/unmap 触发重排，观察窗口尺寸变 `(int)NaN`。
- 修复：`if (cycle.empty()) return;` 并在 mod 前校验；所有除法加 `std::max(1, denom)`/`if (acc > 0)` guard；`setPositionGlobal` 入口对非有限值 `std::isfinite` 断言+回退。

## F-DSK-08（中危）Scrolling：空容器 size_t 下溢越界读与 insert 无界索引

- 位置：
  - `src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.cpp:289-298`（`SColumnData::next()`：`for (size_t i = 0; i < targetDatas.size() - 1; ++i)`，空 vector 时 `size()-1` 下溢为 SIZE_MAX → `targetDatas[0]` 越界读；对比 `down()`（279 行）有 empty guard，`next()` 没有）
  - `ScrollingAlgorithm.cpp:150-165`（`SColumnData::add(SP<ITarget>, int after)`：`targetDatas.insert(targetDatas.begin() + after + 1, ...)` 无 `after ∈ [-1, size)` 校验；`after` 源自调用方传入的 `idx()`——而 `idx()` 未找到时返回 0（`ScrollingAlgorithm.cpp:201-207`），把"不存在"当"位置 0"，恶意时序（对已被移出 column 的 target 调 move）可传入越界 after）
  - `ScrollingAlgorithm.cpp:213`（`return targetDatas.size() - 1;` 于空容器下溢后交给 `setTargetSize(targetDatas.size()-1, ...)` 类调用方）
- 攻击路径：批量 map/unmap/跨 column 移动（drag、`movewindow` IPC）构造 column 变空后继续调用 `next()`/带索引 `add()` → 越界读/插入迭代器 UB → 崩溃或内存破坏。
- PoC 思路：脚本以两个窗口分处相邻 column，向 IPC 高频发 `dispatch scrolling:movewindow l/r` 与窗口自毁交错；ASan 定位越界。
- 修复：`next()` 加 empty guard（与 `down()` 对齐）；`add(t, after)` clamp `after = clamp(after, -1, (int)size()-1)`；`idx()` 返回 `std::optional<size_t>`。

## F-DSK-09（中危）ScrollingFullscreenHandler：null target 的"非确定性 erase"可损坏 m_fsTargets，过期 FS target 存活

- 位置：`src/layout/algorithm/tiled/scrolling/ScrollingFullscreenHandler.cpp:537-566`（`removeFsTarget`：target 为 null 时注释自认"possibly non-deterministic and should NOT happen"，随后 `if (!target) { m_fsTargets.erase(ITR); return; }` 可能删除与调用意图不同的 expired 条目）；`ScrollingFullscreenHandler.cpp:446-470`（`syncFullscreenTargets` 的 `removeFsTargetAndreturnNextIter` 在预取 `std::next(it)` 后调用可能 erase **另一个** 节点的 `removeFsTarget`——当 hash 命中错位时当前 `it` 未被删除，同轮逻辑对其重复处理，依赖 `m_syncingFullscreenTargets` 防重入掩盖）
- 攻击路径：两窗口（其中攻击者控制的）在 fullscreen↔grouped↔destroy 竞态中让 map 中同时存在两个 expired key，`removeFsTarget(nullptr)` 由销毁链触发（unmapWindow→FSMODE_NONE→handler erase）→ 错删存活条目（该窗口失去 FS 状态跟踪，宽度/锁定恢复逻辑丢失）或滞留过期条目（`dataFor(TARGET,...)` 判空逻辑不一致时 recalc 走错分支）。
- PoC 思路：双客户端：同时 toggle fullscreen + `dispatch togglegroup` + 断开连接；比较 `hyprctl workspaces -j`/`getoption` 与实际 FS 状态漂移，ASan 复核。
- 修复：expired key 用存储的 key 迭代删除（`erase_if` 带谓词比较 `it->first` 身份）而非 `find(null)`；删除 `removeFsTargetAndreturnNextIter` 的跨函数 erase 复用，把删除统一收敛到 sync 循环本地。

## F-DSK-10（低中危）Space/Monocle add 无去重 → 重复 target 与幽灵数据

- 位置：`src/layout/space/Space.cpp:39-48,50-59`（`add()`/`move()` 直接 `m_targets.emplace_back(t)`，不查存在性）；`src/layout/algorithm/tiled/monocle/MonocleAlgorithm.cpp:69-77`（`newTarget` 同样不去重；`movedTarget` 直接调 `newTarget`，第 78-80 行）与 `MonocleAlgorithm.cpp:82-96`（`removeTarget` 只删**第一个**匹配）
- 描述：同一 target 被重复 add（重复 `CSpace::move`/monocle move 流程、FS 切换与 group replaceMember 交错的 recalc 路径）后，`removeTarget`/`remove` 一次只清一份 → `m_targetDatas`/`m_targets` 残留幽灵 → monocle `m_currentVisibleIndex` 指向幽灵项、`recalculate` 给已销毁窗口设 alpha/input blocked；`input blocked 未解除` 可让重新入组的窗口永久无法接收输入（`FOCUS_BLOCK_MONOCLE_INACTIVE` 悬置）。
- PoC 思路：monocle 工作区上，脚本对同窗口反复触发 move-target 类 dispatch 后关闭该窗口，验证 `m_targetDatas.size()` > 实际窗口数（IPC 列表比对）且另一窗口点击无输入。
- 修复：`add`/`newTarget` 前查重（或由 `LayoutManager::newTarget` 断言目标 space==null）；`removeTarget` 用 erase-remove 惯用法清全部匹配。

## F-DSK-11（低危）CSpace::recheckWorkArea 允许负尺寸工作区 → 布局除零/NaN 源头

- 位置：`src/layout/space/Space.cpp:77-107`：`reservedGaps.applyip(workArea)` 后无 `std::max(0, ...)` clamp。monitor 热插拔到极小分辨率 + `general:gaps_in/out` 或 workspace-rule gaps 较大时 workArea w/h ≤ 0；dwindle `box.h / box.w`（`DwindleAlgorithm.cpp:184-186`）、scrolling 列宽归一化、master `WORKAREA.w / ...` 全部失去正分母保证。
- 攻击路径：环境型（热插拔/虚拟输出驱动被恶意客户端经由 wlr-output-management 协议操纵分辨率，若启用），触发后续布局 NaN/inf 几何（同 F-DSK-07 下游）。
- 修复：`recheckWorkArea` 末尾 `m_workArea.w = std::max(1, ...)` 类 clamp；各算法入口对非有限值早退。

## F-DSK-12（低危）FocusState PINNED 路径解引用可能为空的 m_focusMonitor

- 位置：`src/desktop/state/FocusState.cpp:165-166`：`if (pWindow->m_state & WINDOW_STATE_PINNED) pWindow->m_workspace = m_focusMonitor->m_activeWorkspace;` — `m_focusMonitor` 在 `rawMonitorFocus(nullptr)`（`FocusState.cpp:276-278`，monitor 全部移除时）或启动早期可为空。X11 客户端可通过 `_NET_WM_STATE_ABOVE`/pinned 流程自设 pinned 态（视实现），在无活动 monitor 的窗口期内请求焦点。
- 修复：`if (... && m_focusMonitor)` guard；else 早退。

## F-DSK-13（低危）Window::onMap 的 doLater 回调以 `!self` 判活，无法排除 destroying 态窗口

- 位置：`src/desktop/view/window/Window.cpp:563-571`：`setCallbackOnBegin([this]{ ... g_pEventLoopManager->doLater([this, self = m_self] { if (!self) return; sendWindowSize(); }); })`。`operator bool == valid()` 在对象 **destroying 标志置位、析构运行中仍返回 true**（hyprutils WeakPtr.hpp 154-171 注释），此窗口期 `this` 可指向半销毁对象；且外层 `[this]` 捕获本身依赖动画变量与 window 同生命周期这一隐式约束。
- 修复：改为 `if (const auto w = self.lock()) w->sendWindowSize();`（lock 同时排除 destroying），去掉 `this` 捕获。

## F-DSK-14（低危）Group init/replaceMember 的边界假设未防御

- 位置：`src/desktop/view/Group.cpp:38-69`（`init()` 假定 `m_windows` 非空：`m_windows.at(0)` 空 vector 时抛 `out_of_range`；调用方 `CGroup::create` 由 actions/IPC 传入列表，恶意 IPC 传空参 → 未捕获异常 → terminate）；`Group.cpp:176-232`（`replaceMember` 未检查 `IDX` 处槽位是否 expired，直接把 `m_windows.at(IDX) = newWindow` 覆盖尸体——掩盖 F-DSK-01 症状）；`Group.cpp:318-343`（`setCurrent` 中 `m_windows.at(m_current).lock()` 可 null → 传 null 进 `setFullscreenMode`/`newWindow->windowTarget()` 第 338 行 null 解引用）。
- PoC 思路：`hyprctl dispatch` 构造空成员组的调用面（取决于 actions 参数校验层）；expired 成员组触发 `togglegroup` current 切换。
- 修复：`create` 拒绝空列表；`setCurrent`/`replaceMember` lock 判空 + gc。

---

## 汇总

| ID | 级别 | 主题 | 关键位置 |
|----|------|------|----------|
| F-DSK-01 | 高危 | destroyWindow 不解除 group → expired WP 裸解引用崩溃 | Window.cpp:1809/1608, Group.cpp:415/462 |
| F-DSK-02 | 高危 | CGroup::destroy 对 expired 成员死循环挂死 | Group.cpp:71-85/234-244 |
| F-DSK-03 | 中危 | swallow PPID 祖先链匹配可伪造 → 吞终端/输入导向 | WindowSwallowController.cpp:47-114 |
| F-DSK-04 | 中危 | swallow restore 对 null m_workspace 解引用/销毁中注入 target | WindowSwallowController.cpp:188/207/309 |
| F-DSK-05 | 中危 | 后端监听器裸 this，SP 先亡则事件 UAF | Window.cpp:133-150 |
| F-DSK-06 | 中危 | 历史容器 WP 裸解引用 + 工作区 gc off-by-one | WindowHistoryTracker.cpp:36-44, WorkspaceHistoryTracker.cpp:57-68 |
| F-DSK-07 | 中危 | master 空 mod SIGFPE / 零分母 NaN 几何 | MasterAlgorithm.cpp:988-994/1078-1134 |
| F-DSK-08 | 中危 | scrolling 空容器下溢越界 + insert 无界索引 | ScrollingAlgorithm.cpp:289/150/213 |
| F-DSK-09 | 中危 | fullscreen handler 非确定性 erase 损坏 m_fsTargets | ScrollingFullscreenHandler.cpp:537-566 |
| F-DSK-10 | 低中危 | Space/Monocle add 不去重 → 幽灵 target | Space.cpp:39-59, MonocleAlgorithm.cpp:69-96 |
| F-DSK-11 | 低危 | 负 workArea 未 clamp → 除零/NaN 源头 | Space.cpp:77-107 |
| F-DSK-12 | 低危 | PINNED focus 解引用空 m_focusMonitor | FocusState.cpp:165-166 |
| F-DSK-13 | 低危 | doLater 用 valid() 而非 lock() 判活（destroying 窗口期） | Window.cpp:563-571 |
| F-DSK-14 | 低危 | Group 空列表/槽位 expired 边界未防御 | Group.cpp:38-69/318-343 |

优先修复建议：F-DSK-01/02 同根源（销毁链不对称 + expired WP 无 guard），一组补丁即可消除两个可利用终点；其次是 F-DSK-07/08 的算法边界（IPC 可达、PoC 成本低）。

审计限制说明：`~CWLSurfaceResource` 与资源销毁顺序在 client 断连时的确切 emit 序、以及 `buildOrientationCycleVectorFromVars` 对全非法入参的行为，标注为待动态验证项；以上 crash/hang 类条目建议在 ASan 构建下按 PoC 复核。
