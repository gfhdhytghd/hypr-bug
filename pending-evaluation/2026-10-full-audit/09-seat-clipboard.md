# 安全审计报告 09 — Wayland 核心数据面（Seat / Clipboard / DnD / Output Management）

- 仓库：/home/wilf/data/Hyprland（Hyprland v0.56.0 fork，main @ 397cb0efb）
- 审计域：src/protocols/core/{DataDevice,Seat,Output}*、src/protocols/{PrimarySelection,DataDeviceWlr,ExtDataDevice,OutputManagement}.cpp、src/managers/SeatManager.cpp、src/managers/input/Touch.cpp、光标/drag 数据流
- 威胁模型：任意 Wayland 客户端（本地、已连接 socket）
- 参考实现：wlroots（wlr_seat / wlr_data_device / wlr_primary_selection / wlr_output_management）
- 排除项（同事已报）：Compositor/SurfaceState/Surface queue（F-PROTO-04）、Shm（F-PR2-04）、IME/TextInput、screencopy、popup
- 说明：本 fork 中不存在 `src/managers/ClipboardManager*`（grep 全仓无此类文件）；Wayland 粘贴数据全部经 pipe 直接传递、compositor 不做缓存，因此"粘贴内容无大小上限"在纯 Wayland 路径上不成立（X11 剪贴板桥接边界见 F-SEAT-12 备注）。

---

## F-SEAT-01 set_selection 不校验请求客户端键盘焦点、serial 完全忽略 → 后台客户端剪贴板/中键粘贴劫持

- 严重程度：**高**
- 位置：
  - src/protocols/core/DataDevice.cpp:248-262（`setSetSelection`：lambda 接收 `uint32_t serial` 但函数体从未使用；不比较 `device->client()` 与 `g_pSeatManager->m_state.keyboardFocusResource->client()`）
  - src/protocols/PrimarySelection.cpp:114-137（同样忽略 serial，且无 pointer focus 归属检查）
  - src/managers/SeatManager.cpp:697-757（`setCurrentSelection`/`setCurrentPrimarySelection` 只做去重，无焦点/权限门）
- 证据：`setSetSelection` 直接 `g_pSeatManager->setCurrentSelection(source)`；随后 `CWLDataDeviceProtocol::setSelection`（DataDevice.cpp:483-521）把 offer 发给"当前键盘焦点客户端"，即任何客户端都能替换受害者看到的 selection。wlroots 的 `data_device_handle_set_selection` 会校验 `device->seat` 的 focused client 且（≥0.15）用 serial 验证最近输入事件；primary selection 同样要求 pointer focus client。
- 攻击路径：恶意后台客户端（从未获得焦点）bind wl_data_device → create_data_source(offer mime: text/plain) → set_selection(伪造 serial) → 其 source 成为全局 selection。受害者（编辑器/终端/密码框）粘贴时 `data_offer.receive` 到攻击者 fd → 攻击者收到 `data_source.send`，返回任意内容（如伪造的凭据、shell 命令、含 `file://` 的拖放 URI）。同时把真实剪贴板 owner 挤出（其 source 收到 cancelled），实现静默劫持与嗅探。
- PoC 思路：写一个小 libwayland 客户端，不 map 任何窗口，循环：`wl_registry` 取 wl_seat/wl_data_device_manager → create_data_source → offer "text/plain" → set_selection(0)；另开一个 receive 打印受害者 source 写来的数据。观察在受害者应用里 Ctrl+V 得到攻击者提供的字符串、且原 owner 的 source 被 cancelled。
- 修复：set_selection / set_primary_selection 校验 (a) 请求来自 seat 的当前焦点客户端，(b) serial 为 `CSeatManager::serialValid()` 中键盘(button/enter)/指针(button/enter) 序列内的有效值并一次性擦除；违规按 wlroots 语义丢弃请求或 post `WL_DATA_DEVICE_ERROR_*`。

## F-SEAT-02 wl_data_device.start_drag 无 serial/按钮/焦点校验 → 无按钮事件的注入式拖放

- 严重程度：**高**
- 位置：src/protocols/core/DataDevice.cpp:264-279（`setStartDrag`：`serial` 参数完全未使用；未校验请求客户端是否持有 pointer 焦点、是否有按下的按钮、origin 是否为焦点 surface）；对照 src/protocols/core/Seat.cpp:242-248（按钮 serial 其实已通过 `recordPointerButtonSerial` 入库，可直接复用）与 src/managers/SeatManager.cpp:185-205（`pointerButtonSerialValid` 已存在，XDGShell move/resize 已在用，但 start_drag 没有用）。
- 证据：`initiateDrag`（DataDevice.cpp:557+）无条件设置 `m_dnd.currentSource`、把光标 override 成 "grabbing"（564-565）、随后 `updateDrag()` 用 `g_pSeatManager->m_state.dndPointerFocus` 对任意 target surface 发送 `data_device.enter` + offer。
- 攻击路径：恶意客户端 create_data_source(offer 如 `text/uri-list`) → start_drag(source, 自己某个已存在 surface, icon, serial=任意)。若其 surface 恰在指针下（可先请求 set_cursor/利用 follow_mouse），target 客户端收到 enter/motion，鼠标下显示拖放合法态；用户基于该"假拖拽"接受 drop（`data_device.drop` 由 dropDrag 在真实按钮释放时发出，DataDevice.cpp:737-767），得到攻击者数据（如指向 /etc/shadow 的 file:// URI 拖入编辑器，或拖入文件管理器触发复制）。与 F-SEAT-03 组合可把假拖拽升级为全局 DoS。
- PoC 思路：客户端创建 1×1 surface 并置于指针下，调用 start_drag 用 `serial=0xDEADBEEF`；验证无任何按钮按下即收到 `data_device.enter`，并在按钮释放时向 target 发出 `drop`。
- 修复：在 `setStartDrag` 中要求：请求客户端 == 当前 pointer focus 客户端；`pointerButtonSerialValid(seatResource, serial, originSurface)` 为真（且按钮仍处于按下态）；origin/icon 属于该客户端。

## F-SEAT-03 拖放结束依赖 target 客户端自愿 `offer.finish`，无超时/取消兜底 → dndActive 永久为真，全局丢弃按钮事件

- 严重程度：**高**
- 位置：src/protocols/core/DataDevice.cpp:737-767（`dropDrag` 成功后 `cleanupDndState(false, false, false)`：`m_dnd.currentSource`/`focusedDevice` 故意保留，等待 target 调 finish→`completeDrag`）；src/protocols/core/DataDevice.cpp:44-77（receive/finish 均无超时）；src/managers/SeatManager.cpp:438-440（`sendPointerButton`：`if (PROTO::data->dndActive()) return;`——拖拽激活期间所有客户端按钮事件被静默丢弃）；src/protocols/core/Seat.cpp:192-198（sendLeave 同样丢弃按钮）；兜底只有 src/protocols/core/DataDevice.cpp:856-860 的 `abortDndIfPresent`（按 Escape，需人工介入）。
- 证据：`dropDrag()` 结尾 `cleanupDndState(false,false,false)`（DataDevice.cpp:766）不重置 source/device，也不挂任何 watchdog；唯一复位路径是 target 的 `offer.finish`（71-77）、`onDndPointerFocus`、或 Escape。`wasDragSuccessful()`（769-787）只要攻击者 offer 曾 accept/receive 即返回 true。
- 攻击路径：恶意客户端自己同时扮演 source 与 target（拖到自己拥有的 surface，`dataDeviceForClient` 命中自己，DataDevice.cpp:429-439），按 F-SEAT-02 注入拖拽；等用户下一次真实按钮释放触发 `dropDrag`→进入"已完成但未结束"状态；恶意 target 接受/receive 后**永不调用 finish** → `dndActive()` 永久 true → 整个桌面所有 wl_pointer 客户端的 button 事件全部被吞（`sendPointerButton` 直接 return），用户只剩键盘；期间 `sendLeave` 还丢弃按钮态。需按 Esc（若攻击者再拦 Escape）或新拖拽/焦点变化才恢复。
- PoC 思路：脚本化攻击客户端：起 drag→accept+receive 自己的 offer→不调 finish；随后在任意 GUI 应用中验证鼠标点击无效（motion 仍来、button 消失）。
- 修复：dropDrag 后对 (source,target) 加超时定时器（EventLoopManager deferred），或让 `sendPointerButton` 在 dndActive 但无活动 dnd surface/已 drop 时强制 `abortDrag()`；target 客户端销毁（device destroy）必须同步清理 currentSource。

## F-SEAT-04 wl_data_offer.receive 无一次性约束、无速率限制、core 路径未 accept 自动代接受 → 对 source 客户端的 fd/事件洪泛 DoS

- 严重程度：**中**
- 位置：src/protocols/core/DataDevice.cpp:44-69（`setReceive`：`m_recvd` 只记录从不校验，重复 receive 每次都 `m_source->send(...)`；58-61 未 accept 时由 compositor 代调 `accepted(mime)`，serial/mime 均不校验）；src/protocols/DataDeviceWlr.cpp:14-29、src/protocols/PrimarySelection.cpp:15-30、src/protocols/ExtDataDevice.cpp:14-29（同样无限 receive，且无 accept 概念）。
- 证据：`CWLDataOfferResource` 的 `m_recvd/m_accepted` 从不阻止第二次 receive；`m_source->send()` 每次 `sendSend(mime, fd)` 向 source 客户端 post 一个新事件并 fd-pass 一个新 fd（wl_data_source.send）。
- 攻击路径：恶意客户端对任一受害者剪贴板（F-SEAT-01 后甚至可主动拿到）创建 offer 后循环调用 `receive` 数万次：每个调用传入新 dup 的 fd（或直接 0/任意可写 fd），受害者 source 客户端事件队列被 `data_source.send` 洪泛，fd 表被灌爆（默认 RLIMIT_NOFILE 1024）→受害者崩溃/无响应。libwayland 连接 buffer 也同步被灌满。
- PoC 思路：攻击端对目标 selection 循环 100k 次 `wl_data_offer_receive(pipe_rd_dup)`，观察受害者 fd 数暴涨、事件队列堆积。
- 修复：(a) `m_recvd` 后拒绝再次 receive（规范语义：receive 之后须 destroy offer）；(b) 每 offer 加速率/次数上限；(c) `setReceive` 校验 mime 必须在本 offer 曾 send 给该客户端的 mimes 内。

## F-SEAT-05 OutputManagement set_custom_mode 无尺寸/刷新率上限，test 为 stub 恒真 → 巨幅 framebuffer OOM / 整数溢出

- 严重程度：**高**
- 位置：src/protocols/OutputManagement.cpp:462-487（仅 `w<=0||h<=0||refresh<0` 检查；wlroots 限 custom mode 宽高 ≤8192、refresh ≤1000Hz 量级）；src/protocols/OutputManagement.cpp:326-333、351-355（`setTest`→`applyTestConfiguration(true)` 直接 `LOG WARN "TODO: STUB ... returning true"`，任何配置都"通过测试"）；388-393（customMode 原样存入 monitor 规则）。
- 攻击路径：恶意 bind zwlr_output_manager_v1 → create_configuration → enable_head → set_custom_mode(65535, 65535, 2000000) → apply → `Config::monitorRuleMgr()->scheduleReload()` 将该 mode 作为监视器规则下发，渲染侧按像素尺寸分配 framebuffer/swapchain（65535×65535×4 ≈ 17GB，且 `int32 w*h` 类中间量存在溢出）→ OOM 或 UB 崩溃；refresh 天文数字使时序计算（msPerFrame）异常。test 路径本应先 DRM test 再放行，此处恒真使其形同虚设。
- PoC 思路：repro 客户端绑定 output_manager，枚举 head，构造超大 custom mode 并 apply；观察 compose 在 headless/DRM 下于帧分配处 OOM。
- 修复：按 wlroots 加 `w,h ∈ (0,8192]`、`refresh ∈ (0, 1000000] mHz` 上限并 post `INVALID_CUSTOM_MODE`；实现真实 `test_configuration`（aquamarine atomic test）而非 stub。

## F-SEAT-06 OutputManagement 无会话锁/权限门控 → 锁屏期间任意客户端禁用或重配所有显示器

- 严重程度：**高**
- 位置：src/protocols/OutputManagement.cpp 全文无 `isSessionLocked()`/`SessionLock` 引用（grep 证实）；对照同仓 seat 侧已修同类问题：commit 7b5b0c192 "seat: reject new grabs when the session is locked"（src/managers/SeatManager.cpp:757-758）；`setDisableHead`（298-324）与 `applyTestConfiguration`（351-430）无条件 `scheduleReload()`。
- 攻击路径：会话锁定（locker 已映射）时，恶意客户端仍可 bind output_manager（全局未被禁用）→ create_configuration → disable_head(所有 head) → apply → 调度禁用全部物理输出 → 锁屏不可见不可解锁（仅 DoS）；或 enable_head + set_transform/set_scale 把屏翻成黑/畸形模式，配合 locker 的重绘时序进行视觉欺骗/强制密码输入。
- PoC 思路：`hyprlock` 起来后跑脚本调用 output_manager disable_head + apply；验证屏幕黑掉且解锁界面消失。
- 修复：锁定时（`g_pSessionLockManager->isSessionLocked()`）拒绝 bind（不发 global）、拒绝 create_configuration/apply；并在 `setEnable/DisableHead` 检查 `m_owner->m_stopped`。

## F-SEAT-07 create_configuration 不校验 done-serial、不限制并发配置、stop 后仍可操作

- 严重程度：**中**
- 位置：src/protocols/OutputManagement.cpp:22-35（`setCreateConfiguration` 收到 `serial` 参数但未与 `sendDone` 记录的 serial（86-88）比对；wlroots 要求等于最近 done serial 且同时至多一个 in-progress config）；262-345（`m_stopped` 仅存于 manager（20），enableHead/apply 均不检查）。
- 攻击路径：客户端用任意/过期 serial 反复创建配置对象并排队（`m_configurations` 无界增长 → 内存 DoS），并用过期 config 在 head 已变化（热插拔、monitorDestroy→sendFinished，97-116）后仍然 apply，操作已失效的 head 描述符，触发竞态下的重配置（结合 F-SEAT-05 放大）。
- PoC 思路：连续 `get_configuration` 数千次不 apply，观察 `PROTO::outputManagement->m_configurations` 无界增长；或 stop 后继续 enableHead 验证无错误。
- 修复：记录每 manager 的最近 done serial 与至多一个 in-progress configuration；不匹配则 post 协议错误；`m_stopped` 后拒绝所有创建/应用。

## F-SEAT-08 set_mode 不校验 mode 与 head 的归属 → 跨 head 模式注入

- 严重程度：**中**
- 位置：src/protocols/core/OutputManagement 相关：src/protocols/OutputManagement.cpp:438-459（`setSetMode` 用 `modeFromResource(outputMode)` 在全局 `m_modes` 中查找，未比较 `MODE->head/m_monitor` 与 `m_monitor`；wlroots 检查 `mode->head != head → invalid_mode`）。COutputMode 构造（233-239）也未记录所属 head。
- 攻击路径：多显示器场景，客户端对 head A（内建 1080p）enable 后传 head B（外置支持 4K@144 的 mode 资源）做 set_mode → apply 把不属于该 head EDID 的模式写入规则 → 后端原子测试失败/无信号/闪屏；重复触发形成对特定输出的稳定性 DoS。
- PoC 思路：双 monitor 下，取 head B 的 mode resource 传给 head A 的 configuration head，观察规则写入成功且 reload 触发失败模式。
- 修复：`COutputMode` 保存 owner head，`set_mode` 校验同 head，否则 post `ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_INVALID_MODE`。

## F-SEAT-09 set_position 不校验布局合法性（重叠/越界）→ 监视器布局破坏

- 严重程度：**中**
- 位置：src/protocols/OutputManagement.cpp:489-504（任意 int32 x/y 直接入库；wlroots 校验 head 位置须在布局内、不得重叠）。
- 攻击路径：apply 两个 head 相同 position 或 INT32_MIN/MAX 坐标 → 布局计算、光标边界、`query().vec()` 等使用点出现溢出与不可达区域；与 F-SEAT-06 组合可在锁屏下把光标/焦点困在重叠区。
- 修复：校验 position ∈ [INT16_MIN, INT16_MAX]（与 wl_output 几何一致）并拒绝重叠布局，violation post `INVALID_POSITION`。

## F-SEAT-10 pointer.set_cursor 的 serial 校验被注释禁用 → 光标图像注入无输入合法性约束

- 严重程度：**中**
- 位置：src/managers/SeatManager.cpp:678-691（`onSetCursor` 中 `serialValid` 调用整段被注释，附 "TODO: fix this"）；调用点 src/protocols/core/Seat.cpp:119-138。
- 证据：仅剩 `pointerFocusResource->client() == seatResource->client()` 的弱检查；`nextSerial`（SeatManager.cpp:112-132）记录 enter/button serial 却不被消费。
- 攻击路径：拥有焦点的客户端可在任意时刻（无需刚收到 enter/button）替换系统光标图像；更关键的是被注释的 serial 校验意味着"用旧 serial 重放 set_cursor"畅通——配合焦点竞态（pointer focus 更新与 libwayland 派发顺序），焦点已离开后仍能再注入一次光标覆盖（视觉欺骗：用全黑光标、假 I-beam/假 busy 光标实施 UI 钓鱼）。光标 buffer 尺寸无上限（见 F-SEAT-11）。
- PoC 思路：焦点客户端保存 enter serial，在失去焦点的同时用旧 serial 调 set_cursor，观察 `onSetCursor` 仅在 focus 已切换后才拒绝、serial 从不校验。
- 修复：恢复 `serialValid`（记录 enter/button 的 serial 池并在 set_cursor 消费，允许一次性使用），与 wlroots `wlr_seat_validate_pointer_grab_serial` 对齐。

## F-SEAT-11 光标 shm 像素数据拷贝无尺寸/格式校验；CPU 光标路径每帧原地翻转角色缓冲

- 严重程度：**中**
- 位置：src/protocols/core/Compositor.cpp:788-824（`updateCursorShm`：`shmData.resize(bufLen)` 无上限；`beginDataPtr(0)` 后按 `4 * box.x1`、stride 直接 memcpy，假定 32bpp（注释 "bpp is 32 INSALLAH"），不检查 `shmAttrs.format`；仅 `damage.intersect(CBox{0,0,buf->size...})`）；src/pointer/PointerManager.cpp:621-648（`flipRB` 对 `CCursorSurfaceRole::cursorPixelData()` 即角色存储**原地** `std::swap(shmBuffer[i], shmBuffer[i+2])`）。
- 攻击路径：恶意客户端以 cursor 角色提交超大 shm buffer（wlroots 对 cursor 无硬性上限，仅受 shm pool；与 F-PR2-04 的 shm 面叠加）→ 每次 commit 全量 resize+memcpy，巨缓冲 → 放大拷贝（客户端可用高频 commit 小 damage 区触发 `updateCursorShm`（Compositor.cpp:673）→ CPU/内存带宽 DoS）；非 ARGB 格式（C8/RGB565）时按 4bpp 步进读写 → 读越 `pixelData` 边界（取决于 `beginDataPtr` 实际返回的 bufLen 与 stride 假设）造成信息性/稳定性问题；flipRB 每帧翻转同一缓存导致 R/B 抖动——正确性 bug，且证明客户端数据被可写地共享进渲染路径。
- PoC 思路：客户端 set_cursor 8192×8192 shm 面，循环 commit 1px damage；测量 compose CPU。再试 format=WL_SHM_FORMAT_C8 验证格式未校验。
- 修复：限定 cursor buffer 尺寸（≤512×512，参照 XCursor 惯例）、校验 format 为 ARGB8888/ABGR8888 且 `stride ≥ w*4`；flipRB 应在 `updateCursorShm` 拷贝时一次性完成而非每帧原地改。

## F-SEAT-12 触摸焦点记账是"单槽"模型：per-finger 状态共享一个槽位，合成 up 依赖会被覆盖的监听器

- 严重程度：**中**
- 位置：src/managers/SeatManager.cpp:507-557（`sendTouchDown/Up`：`m_state.touchFocusResource` 与 `m_touchLocks` 均为全局单槽/单计数，不区分 `id`；第二指按下即覆盖第一指的焦点与 destroy 监听器语义）；src/protocols/core/Seat.cpp:27-58（`CWLTouchResource::sendDown`：`m_listeners.destroySurface` 单监听器被第二次 down 覆盖 → 第一指的"surface 销毁即合成 up"兜底丢失；`m_fingers` 计数与 `timeMs+10` 假 up）；src/managers/input/Touch.cpp:107-147（`m_touchData.touchFocus*` 同样单槽，锁屏与 LS 分支下多指互相踩）。
- 攻击路径：恶意客户端持有自己的 touch focus surface，诱导物理多指（或在 F-SEAT-02/03 的注入式 drag 中触达 touch 分支）后 destroy 该 surface：因监听器已被覆盖，某根手指的 up 永不到达 → 客户端侧按钮/触摸态卡死；`m_touchLocks` 单计数在 destroy 合成 up（SeatManager.cpp:527 `timeMs+10`）与真实 up 竞争下出现下溢保护后的"吞掉真实 up"（536-537）→ 与 `m_touchLocks<=0` 的 touchFocusChange 判定错位。伪造的 `timeMs+10` 时间戳还会污染客户端的时序假设。
- PoC 思路：客户端持有 touch 的 surface 后（物理双指按下 A/B），destroy surface A；观察 A 的 finger 无 up，`m_touchLocks` 与真实手指数漂移。
- 修复：把 touch 焦点改为 `unordered_map<id, {surface, destroyListener}>`（wlroots 的 `wlr_touch_point` 模型），合成 up 仅在对应点存活时发出。

## F-SEAT-13 wl_data_device_manager.get_data_device 完全不使用 seat 参数；数据面无需 seat 即可建立

- 严重程度：**低**
- 位置：src/protocols/core/DataDevice.cpp:369-387（`setGetDataDevice(r, id, seat)` 中 `seat` 读取后从不验证，wlroots 对非法 seat post `invalid_seat`）；src/protocols/DataDeviceWlr.cpp:180-199、src/protocols/ExtDataDevice.cpp:180-201（同样忽略 seat，但 wlr/ext 客户端 bind 时即 `sendInitialSelections()` 把当前剪贴板 offer 主动灌入——放大 F-SEAT-01/04 的读取面）。
- 攻击路径：客户端从不 bind wl_seat 也能得到 data device 与 selection 通知；`dataDeviceForClient`（DataDevice.cpp:429-439）按 client 线性取首个匹配，同客户端多 device 时行为不确定，配合 F-SEAT-04 的洪泛可让 compositor 向错误的 device 发 enter/leave。
- 修复：校验 seat resource 存在且属于请求客户端；无 seat 报 `WL_DATA_DEVICE_MANAGER_ERROR_INVALID_SEAT`。

## F-SEAT-14 serial 存储记账缺陷：enterSerial 永不消费、serials 上限 100 可被挤掉 → serial 重放

- 严重程度：**低~中**
- 位置：src/managers/SeatManager.cpp:112-154（`serialValid` 中 `container->enterSerial == serial` 分支返回 true 但**不擦除**，enter serial 可无限重放；`serials` 环形上限 `MAX_SERIAL_STORE_LEN=100`（SeatManager.hpp:11），高频 motion 期间（每次 enter 只入一个、但 button/key/mod serial 入列）旧 serial 被挤出后合法 serial 失效，未挤出的则可重复使用）；调用方 src/protocols/PointerWarp.cpp:71、src/protocols/XDGActivation.cpp:35、src/protocols/XDGShell.cpp:202/227、PointerConstraints 路径等。
- 攻击路径：客户端保存自己收到的 enter serial，反复用于 xdg_activation/pointer_warp/（修复 serial 后的）set_cursor；对 activation 侧而言，一次合法 enter 可无限次"证明用户交互"，弱化 activation token 的时效性约束。
- PoC 思路：记录 pointer enter serial，多次用同一 serial 调 `zwp_xdg_activation_v1.request_activate`，均通过 `serialValid`。
- 修复：enterSerial 与 serials 统一为一次性集合（用后 erase）；容量上调至 1000 级别；统一封装 `consumeSerial()`。

## F-SEAT-15 （边界备注）X11↔Wayland 剪贴板桥与粘贴大小上限不在本域；本 fork 无 ClipboardManager

- 严重程度：**信息**
- 位置：全仓 `grep -i clipboard`：仅 src/xwayland/XDataSource.cpp、XWM.cpp（xwayland 域，同事 05/08 已覆盖）；`src/managers/ClipboardManager*` 不存在。
- 说明：任务提示的"粘贴数据无大小上限→内存放大"在 Wayland 核心数据面（本域）不成立：offer/receive 均为 fd-to-fd，compositor 不缓冲数据内容。唯一的 compositor 侧数据缓冲是光标 shm（见 F-SEAT-11）与 XWayland 桥（见 xwayland 域）。若后续版本引入剪贴板管理器（缓存粘贴内容），须同步补大小上限与配额。

---

### 修复优先级建议
1. F-SEAT-01/02/03/05/06（高，客户端可远程触发桌面级 DoS 与剪贴板/拖放完整性破坏）
2. F-SEAT-04/07/08/09/10/11/12（中）
3. F-SEAT-13/14（低）
