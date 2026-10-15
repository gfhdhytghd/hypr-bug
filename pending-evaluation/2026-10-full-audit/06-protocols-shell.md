# 06 — Shell 协议实现安全审计(xdg / layer-shell / foreign-toplevel / surface state)

- 仓库:`/home/wilf/data/Hyprland`(Hyprland v0.56.0 fork,重构版目录结构)
- 范围:`src/protocols/XDGShell.*`、`LayerShell.*`、`ForeignToplevel*`、`src/protocols/types/SurfaceState*`、`src/protocols/core/Compositor.cpp`、`Subcompositor.cpp`、`src/desktop/view/WLSurface.*`、`Popup.*`、`Subsurface.*`、`WaylandPopupBackend.*`
- 威胁模型:任意连接的恶意 Wayland 客户端(无需 root),目标是让合成器崩溃 / 挂起 / 内存耗尽 / 输入窃取。

严重程度评级:High = 远程单客户端即可崩溃或永久挂起整个合成器会话;Medium = 资源耗尽 / UB / 越界几何;Low = 协议偏差、放大受限。

---

## F-PROTO-01 — xdg_popup 自父引用导致 `accumulateParentOffset` 死循环(挂起 DoS)

- 严重程度:**High**(单客户端一条请求序列永久冻结合成器)
- 位置:
  - `src/protocols/XDGShell.cpp:605-628`(`setGetPopup`:对 `parentXDG` 无任何校验,允许 `parent == 自己的 surface`,也不检查 surface 已有 toplevel/popup 角色)
  - `src/protocols/XDGShell.cpp:106-118`(`accumulateParentOffset()`:`while (current)` 沿 `surface→popup→parent surface` 链累加,无 visited 集合;自父时 `current` 恒等于同一 `CXDGSurfaceResource`,永不退出)
  - 触发点 `src/protocols/XDGShell.cpp:93-104`(`applyPositioning`)← `src/desktop/view/Popup.cpp:370-380`(`CPopup::reposition()`)← `src/desktop/view/Popup.cpp:363-369`(`onReposition`,由 `xdg_popup.reposition` 请求经 `src/desktop/view/popup/WaylandPopupBackend.cpp:16` 转发)
- 攻击路径:
  1. `getXdgSurface` 创建 xdg_surface S;
  2. 用 S 自身作为 parent 调 `get_popup`(代码中 `parent = CXDGSurfaceResource::fromResource(parentXDG)` 可为 `m_self`,无检查;`XDGShell.cpp:617` 又把 `S->m_popup = P`,构成 S→P→S 环);
  3. 给 S 调 `get_toplevel`(`XDGShell.cpp:598-602` 会把已登记的 popup 补发 `newPopup`,使桌面层为 P 创建 CPopup,链路打通);
  4. 映射窗口、attach buffer,再发送 `xdg_popup.reposition` → `accumulateParentOffset()` 无限循环。
  另外 `src/desktop/view/Popup.cpp:406、450、537` 三处 `while (curr->m_parent)` 母链遍历同样无环检测,一旦桌面层母链成环(见 F-PROTO-06 的类比场景)同样冻结。
- PoC 思路:libwayland 客户端脚本:bind xdg_wm_base → create_surface S → get_popup(parent=S, positioner) → get_toplevel(S) → commit(buffer) → popup.reposition(1)。合成器主循环 100% CPU 卡死,整个会话无响应。
- 影响:全系统 DoS(挂起);所有客户端渲染/输入停止。
- 修复建议:`setGetPopup` 拒绝 `parent == m_self`(post `XDG_SURFACE_ERROR_INVALID_SURFACE` 或协议错误);`accumulateParentOffset` 与 Popup.cpp 母链遍历加 `visited` 集合或深度上限(wlroots 风格)。

## F-PROTO-02 — popup 内容 xdg_surface 销毁后 grab/reposition 解引用过期弱指针(崩溃)

- 严重程度:**High**(远程崩溃合成器)
- 位置:
  - `src/protocols/XDGShell.cpp:970-989`(`addOrStartGrab`:`popup->m_surface->m_surface.lock()`,`m_surface` 是 `WP<CXDGSurfaceResource>`(见 `XDGShell.hpp:70`),**无 null 检查**)
  - `src/protocols/XDGShell.cpp:93-94`(`applyPositioning`:`m_surface->m_pending.geometry.pos()` 同样直接解引用 WP)
  - 对照组:`src/protocols/XDGShell.cpp:1002`(`onPopupDestroy` 里却有 `if (popup->m_surface)` 判空)与 `WaylandPopupBackend.cpp:29-32`(`valid()` 会检查)——说明该成员可空是被认可的,以上两处是遗漏。
- 攻击路径:客户端创建合法 popup(父为已映射 toplevel 的 surface,使桌面层 CPopup 存在)→ 发送 `xdg_surface.destroy` 销毁 **popup 自己的内容 xdg_surface**(协议上不禁止 popup role 对象继续存活;`CXDGSurfaceResource` 从 `PROTO::m_surfaces` 移除后 popup 的两个 WP 过期)→ 随后对 popup 发送 `grab(seat, serial)` → `XDGShell.cpp:975` 对空 shared_ptr 解引用 → SIGSEGV;或发送 `reposition` → `XDGShell.cpp:94` 崩溃。`setGrab` 回调(`XDGShell.cpp:79-82`)完全忽略 `seat`/`serial` 参数,无任何合法性检查,保证请求可达。
- PoC 思路:`xdg_popup.grab` 载荷中 seat/serial 填任意值;先发 `xdg_surface.destroy(content)` 再 flush `grab`。gdb 下可见空指针解引用。
- 影响:合成器崩溃 = 全 session 掉线;同时 grab 路径还会向 `CSeatGrab::add(nullptr)` 注入空 surface(`src/managers/SeatManager.cpp:882`)造成抓取行为异常。
- 修复建议:`addOrStartGrab`/`onPopupDestroy`/`applyPositioning` 统一先 `lock()` 判空;grab 时校验 `m_surface && m_surface->m_surface` 均存活;考虑按规范校验 serial 归属。

## F-PROTO-03 — popup.grab 无归属校验,任意客户端加入他人活动 grab(输入窃取)

- 严重程度:**Medium-High**
- 位置:`src/protocols/XDGShell.cpp:970-989`(`addOrStartGrab`:仅当 `m_grabOwner` 为空时设 owner;否则无条件 `m_grabbed.emplace_back(popup)` 并把 popup 的 surface 及 parent surface 加进同一 `m_grab`)
- 攻击路径:受害者应用弹出带 grab 的菜单(compositor 已有 `m_grabOwner`)。恶意客户端自己创建一个 popup 并调用 `xdg_popup.grab` → 其 surface 被 `m_grab->add()` 注入**别人正在使用的 grab** 的 surface 集合 → 在 grab 期间该 popup 可成为 pointer/keyboard focus 候选,窃取或截获本应只投递给菜单链的事件;恶意 popup 还可借此长期持有 grab 引用(owner 已 dismiss 后 `m_grabOwner` 不会被复位,依赖 WP 过期间接失效,见 `XDGShell.cpp:991-1004`)。
- PoC 思路:两个进程。A:普通菜单 popup grab。B:无边框 popup 每 100ms 重绘覆盖菜单区域并 `grab`,验证键盘事件被 B 收到。
- 影响:grab 语义破坏、输入窃听/遮挡(点击劫持辅助)。
- 修复建议:与 `m_grabOwner` 客户端不一致的第二个 grab 请求应拒绝或强制先 dismiss 旧 grab(规范要求同 seat 只有一个活跃 grab);`addOrStartGrab` 增加 owner 客户端/seat 一致性校验。

## F-PROTO-04 — 每 surface 提交状态队列无上限:未 signal 的 fence 冻结队列 + O(n²) 解锁(内存/FD DoS)

- 严重程度:**Medium**
- 位置:
  - `src/protocols/core/Compositor.cpp:194`(`m_stateQueue.enqueue(...)`,无任何深度上限)
  - `src/protocols/types/SurfaceStateQueue.cpp:17-19`(直接 `emplace_back`)、`51-65`(`unlockFence` 每次遍历整个队列)、`79-91`(`find` 线性扫描)
  - `src/protocols/core/Compositor.cpp:603-612`(dmabuf 显式 fence 不 signal → `LOCK_REASON_FENCE` 永久持有队首)
- 攻击路径:恶意客户端经 linux-dmabuf 提交一个 `exportSyncFiles()` 永不出事的 acquire fence(`Compositor.cpp:200-204` 锁定队首),然后无限次 `attach` 新 buffer + `commit`。每次 commit 都 `enqueue` 一个新的 `SSurfaceState`,内含 `CHLBufferReference`(持有 dmabuf/GEM 句柄)与 damage region。队列无上限 → 内核 dma-buf/内存耗尽;同时每次可读事件触发 `unlockFence` 全队列扫描,呈 O(n²) CPU 放大。`mergeFrom`(`SurfaceState.cpp:122-179`)在长队列上还会累积 `damage`/`bufferDamage` 的 pixman 矩形。
- PoC 思路:创建 eventfd 不写、作为 fence fd 随 dmabuf 提交;循环 10 万次 commit,观察 RSS 与 `dma_buf` 计数线性增长、事件循环变慢。
- 影响:内存/FD 耗尽拖垮会话;事件延迟劣化波及所有客户端。
- 修复建议:为每 surface 队列设上限(如 128,超限丢弃最旧状态或向 surface 发协议错误);`unlockFence`/`find` 用索引或位标记消除 O(n²)。

## F-PROTO-05 — layer-shell margin/exclusive 未校验:有符号溢出 UB 与荒谬几何

- 严重程度:**Medium**
- 位置:
  - `src/protocols/LayerShell.cpp:100-107`(`setSetExclusiveZone`、`setSetMargin` 原样存 `int32_t`,零校验;`setSetSize:85-88` 把 `uint32_t` 强转 `int`,负值仅靠 commit 期 `<=0` 检查兜底)
  - `src/render/Renderer.cpp:2826-2847`(`arrangeLayerArray`:`box.width = bounds.width - (margin.left + margin.right)`、`box.y += margin.top`、`box.y -= margin.bottom` 等,`margin.*` 可达 INT32_MAX,`margin.left + margin.right` 触发**有符号溢出 UB**;通过 `box.width <= 0` 检查后 `applyExclusive(...)` 仍以未钳制的 margin/exclusive 缩减共享 `usableArea`)
- 攻击路径:恶意客户端把 `margin.top/right/bottom/left` 各设 2^31-1、`exclusive` 设 INT32_MAX、anchor 只贴单边 → 溢出绕回产生巨大/负几何;`applyExclusive` 用脏值侵蚀其他客户端(如 bars)共用的 usable area → 桌面其他 layer 尺寸被破坏、configure 抖动。
- PoC 思路:`set_margin(INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX); set_exclusive_zone(INT32_MAX); set_anchor(TOP|LEFT); commit`,配 UBSan 构建可捕获溢出点。
- 影响:UB(优化下未定义)、其他客户端布局破坏、潜在渲染路径超大 region。
- 修复建议:入口即钳制 margin/exclusive 到 `[-4096, 屏幕尺寸]` 类范围(参照 KDE/wlroots 的宽松上限),或用 int64 中间量并在 `applyExclusive` 前钳制。

## F-PROTO-06 — xdg_toplevel set_parent 递归后代检测:深链栈溢出 + WP 未判空

- 严重程度:**Medium**
- 位置:`src/protocols/XDGShell.cpp:335-347`(`exploreChildren` 为无深度限制的递归 `std::function`,递归遍历整棵子树;`for (const auto& c : tl->m_children)` 处 `tl`/`c` 均为 `WP<CXDGToplevelResource>`,未判 expired 即 `tl->m_children` 解引用)
- 攻击路径:恶意客户端创建 N 个 toplevel(数万级,普通内存压力),依次 `set_parent` 形成 N 层深链;随后对一个根节点再发 `set_parent(另一链头)` → `exploreChildren` 递归深度 ≈ N,每帧含 `std::function` 开销 → **栈溢出崩溃**。对比 `setNewParent:348` 的环检测依赖同一递归,检测本身成为攻击面。
- PoC 思路:批量 `create_surface + get_toplevel + set_parent(prev)` × 100k,再 `A.set_parent(B)`;默认 8MB 栈下约数万层即崩。
- 影响:崩溃 DoS;若窗口析构与请求同批处理,expired WP 解引用可升级成 UAF(未验证触发)。
- 修复建议:`exploreChildren` 改为带显式栈的迭代 + 深度上限;迭代时对 `c.lock()` 判空并顺带从 `m_children` 清除过期项。

## F-PROTO-07 — get_toplevel/get_popup 无角色重复校验(单 surface 无限生成窗口)

- 严重程度:**Medium**
- 位置:`src/protocols/XDGShell.cpp:579-603`(`setGetToplevel` 不检查 `m_toplevel`/`m_popup` 已存在,每次都 `emplace_back` 新 `CXDGToplevelResource` 并 `CWindow::create(...)` 生成完整窗口对象)与 `605-628`(`setGetPopup` 同理不检查,`m_popup = RESOURCE` 直接覆盖旧值,旧 popup 成为孤儿但其 `m_parent->m_popups` 中残留过期 WP)
- 攻击路径:对同一 xdg_surface 循环发送 `get_toplevel(新 id)` × 10⁵ → 每个都生成一个 CWindow(共享表、动画、focus 结构、xdg configure 定时器),旧 toplevel 因 `m_toplevel` 被覆盖而无法经 surface 正常驱动/销毁,仅能靠 resource destroy 回收 → 内存/窗口管理器负载线性膨胀;混合 `get_popup` 后角色互相覆盖导致视图树状态不一致(surface 同时被 window 与 popup 后端引用)。xdg 规范/`wlroots` 会对同一 surface 二次赋角色 post role error,此处缺失。
- PoC 思路:`while true; do xdg_surface.get_toplevel(next_id); done`,观察 `hyprctl clients` 数量与 RSS。
- 影响:内存 DoS、窗口生命周期混乱(潜在二次 UAF 面,如旧 toplevel 的 `m_owner->scheduleConfigure()` 仍可达 `XDGShell.cpp:385`)。
- 修复建议:请求入口 `if (m_toplevel || m_popup) { post XDG_SURFACE_ERROR_INVALID_SURFACE_STATE; return; }`。

## F-PROTO-08 — wl_region 矩形数量无上限 + layer-shell configure serials 表无上限(低效放大)

- 严重程度:**Low-Medium**
- 位置:
  - `src/protocols/core/Compositor.cpp:71-72`(`setAdd`/`setSubtract` 无限累加 pixman 矩形,无上限)
  - `src/protocols/core/Compositor.cpp:258-272`(`set_input_region` 每次 commit 复制该 region 进状态并与 surface 尺寸求交,`SurfaceState.cpp:48-56`)
  - `src/protocols/LayerShell.cpp:195-203 / 132-144`(`configure` 向 `m_serials` 无限 push,只有客户端 ack 才删除;恶意 layer 客户端反复改 `set_size` 触发 arrange→configure 而不 ack)
- 攻击路径:单 region 内 `add` 百万矩形 → `set_input_region` 后,指针每帧移动、每次 commit 都做 region intersect,CPU 与 pixman 内存线性膨胀;layer 端不 ack 使 `m_serials` 无限增长并反复 configure。
- PoC 思路:循环 `wl_region.add(0,0,1,1)` × 10⁶ 后设为 input region,移动鼠标进入该 surface。
- 影响:CPU/内存 DoS(受限但可持续)。
- 修复建议:对单 region 矩形数设上限(如 4096,超出 post `WL_REGION_ERROR_INVALID_SIZE` 类错误);`m_serials` 超阈值(如 64)直接对 surface 报错。

## F-PROTO-09 — xdg_surface.ack_configure 序列号校验过弱 / uint 回绕

- 严重程度:**Low**
- 位置:`src/protocols/XDGShell.cpp:630-635`:`if (serial < m_lastConfigureSerial) return;` —— (a) uint32 减法语义下,客户端在 configure 前抢先 ack `0xFFFFFFFF`(初始 `m_lastConfigureSerial=0`)等从未发送过的 serial 会被接受;(b) 静默接受旧 serial(规范应 post `ack_configure` 协议错误),`m_events.ack.emit()` 携带伪造 serial 进入窗口状态机(`ackedSize` 路径,`SurfaceState.cpp:166/237`),可干扰 configure/resize 完成判定、误导 `small()`/缩放修正逻辑(`src/desktop/view/WLSurface.cpp:43-85`)。
- PoC 思路:新窗口立即 `ack_configure(0xFFFFFFFF)`,再观察 window 对 pending configure 的“已确认”状态错位。
- 影响:状态机污染、resize 协商卡滞;无直接内存破坏。
- 修复建议:维护未确认 serial 集合(如 layer-shell 的 `m_serials` 做法),不在集合内则 post 协议错误。

## F-PROTO-10 — fromResource 直接信任 `wl_resource_get_user_data`(类型混淆依赖 libwayland 接口校验)+ LayerShell get_popup 缺 null 检查

- 严重程度:**Low(纵深防御缺失)**
- 位置:
  - `src/protocols/XDGShell.cpp:121、370、656、716`、`src/protocols/core/Compositor.cpp:327` 等:模式均为 `sc<CRole*>(sc<CXdgPopup*>(wl_resource_get_user_data(res))->data())->m_self.lock()`,既不判 `wl_resource_get_user_data` 返回值,也不核对 `res->interface`。当前唯一防线是系统 libwayland ≥1.21 对带 `interface=` 声明的 object 实参做的服务端接口校验;在旧/魔改 libwayland 或任何未声明 interface 的实参(如部分未 fork 的扩展 XML)上,恶意客户端可把 xdg_toplevel/wl_buffer 等对象塞进 xdg_surface 槽位 → 结构体错位读取(`data()` 偏移不同)→ 伪造 `WP::lock()` → 类型混淆/UAF 读。
  - `src/protocols/LayerShell.cpp:120-129`:`CXDGPopupResource::fromResource(popup_)` 结果未判 null 即 `popup->m_taken`(nullable 场景/对象已 inert 时直接崩)。XML 声明 `interface="xdg_popup"`(`subprojects/wlroots/protocol/wlr-layer-shell-unstable-v1.xml:297`)使常规路径被 libwayland 拦住,但该 handler 自身无防御。
- PoC 思路:若目标系统 libwayland <1.21:`zwlr_layer_surface_v1.get_popup` 传入自家 `xdg_toplevel` 资源 id。
- 影响:取决于运行时 libwayland 版本;理论上远程可致崩溃,最坏为可利用的类型混淆。
- 修复建议:`fromResource` 统一加 `if (!res || res->interface != &xxx_interface) return nullptr;`,所有调用点补 null 分支。

---

### 附:未发现问题的检查点(负面结论,供复核)

- `xdg_toplevel.set_resize` resize edge 枚举已校验(`XDGShell.cpp:17-31、213-217`);`set_move/set_resize` 有 seat 归属 + serial 校验(`:193-237`)。
- `xdg_positioner.set_size/set_anchor_rect` 拒绝 ≤0(`:685-700`);SUSPENDED/wm_capabilities 有版本门(`:165、176、453`)。
- `wl_surface.set_buffer_scale ≤0` 已拒绝、damage 经 `addSafeDamage` 溢出钳制(`Compositor.cpp:25-36、226-231`)。
- `wl_subcompositor.get_subsurface` 有 bad_parent/角色/t1Parent==child 检查,`posRelativeToParent` 有 visited 集防环(`Subcompositor.cpp:126-133、160-191`);但 `t1Parent()`(`:141-151`)循环体内 `sc<CSubsurfaceRole*>(m_parent->m_role...)` 误用外层 `m_parent` 而非游标 `surf`(`:147`),导致 3 层以上深链时 t1 祖先计算错误,可能使 `:188` 的防环检查被绕过(未构造出端到端 PoC,建议按 bug 修复并补测试)。
- `CXDGToplevelResource::scheduleStateApplication` / `scheduleConfigure` 使用 `doLaterLock`(`XDGShell.cpp:471-489、660-669`),`UP<SEventLoopDoLaterLock>` 随资源析构自动取消,未见延迟回调整数化 UAF。
- foreign-toplevel(ext-table v2 与 wlr 版)客户端面请求仅 `destroy`/`set_fullscreen`/`unset_fullscreen`,window/output 均 `lock()`+判空(`ForeignToplevelWlr.cpp:35-57`),未发现可利用缺陷;wlr 版 `set_fullscreen` 允许任意绑定客户端操纵他人窗口全屏属该协议固有设计。
