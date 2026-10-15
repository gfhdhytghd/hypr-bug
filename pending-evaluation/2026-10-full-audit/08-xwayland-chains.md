# 审计域 08：XWayland / X11 攻击链路（深度审计）

威胁模型：
- **T1** 任意 X11 客户端进程（XWayland 内的恶意 GUI 应用）
- **T2** 同机其他本地用户
- **T3** X11 应用 ↔ Wayland 窗口/合成器交互面（选区、DnD、窗口身份）

范围：`src/xwayland/{Server,XWM,XSurface,XDataSource,Dnd,XWayland}.{cpp,hpp}`，交叉核对 `src/desktop/state/FocusState.cpp`、`src/render/Renderer.cpp`、`src/managers/input/InputManager.cpp`、`src/protocols/core/Compositor.cpp`。
与同事报告 `05-xwayland.md`（F-XW-01，readProp format 混淆）不重复；本报告补充其未覆盖的新漏洞点及两条独立攻击链。

严重程度排序：F-XW2-01（高）→ 02（高）→ 03（中高）→ 04/05/06（中）→ 07/08（中低）→ 09/10/11（低）。

---

## F-XW2-01 ｜公共 X11 socket 0666 + Xwayland 无 Xauthority：任意本地用户可连 DISPLAY（剪贴板窃取 / XTEST 注入 / XRecord 键盘记录） — 严重程度：**高**（威胁 T2 → 用户级完全接管）

- **位置**：
  - `src/xwayland/Server.cpp:66` — `chmod(addr->sun_path, 0666)`，`/tmp/.X11-unix/X<N>` 对所有用户可连；
  - `src/xwayland/Server.cpp:310-311` — 启动命令行 `exec Xwayland :N -rootless -core -listenfd ... -displayfd ... -wm ...`，**没有 `-auth` 参数**；
  - `src/xwayland/Server.cpp:140-150` — `xwayland:create_abstract_socket` 默认 `false`（`src/config/values/ConfigValues.cpp:640`）；即便开启，abstract socket 无文件系统权限位，同样全局可连。
- **描述**：代码注释（`Server.cpp:63-65`）声称"XWayland 会自行用 xhost ACL 控制 socket 访问，因此 0666 安全"。该前提不成立：X 服务器的访问控制只在**提供了 authorization 文件**时生效。Xwayland 不带 `-auth` 启动时，默认安全策略是**接受所有本地 socket 连接**（`xhost` 默认等效 `+local:`），同机其他 UID 的用户经 `/tmp/.X11-unix` 或 abstract socket 连接即被当作合法本地客户端。wlroots 等其他合成器为 XWayland 生成 xauth cookie 并以 0600 socket 启动，本仓库没有这条防线。
- **攻击路径**：攻击者只需在同机拥有一个普通账户（T2），无需受害用户任何操作。socket 路径可由 lock 文件名（`/tmp/.X0-lock`，世界可读，`Server.cpp:197`）或暴力 `:0..:32` 探测得到。
- **PoC**（以 `eviluser` 登录第二终端）：
  ```bash
  # 探测 DISPLAY
  ls /tmp/.X*-lock; DISPLAY=:0 xdpyinfo | head
  # 键盘记录（XInput/XRecord，全 XWayland 范围，含密码框）
  DISPLAY=:0 xinput test-xi2 --root          # 或 python-xlib XRecord 上下文
  # 按键注入到受害者聚焦的窗口（勒索/社工/终端命令注入）
  DISPLAY=:0 xdotool key --clearmode super Return
  # 剪贴板窃取 / 抢所有权
  DISPLAY=:0 xclip -selection clipboard -o
  DISPLAY=:0 xterm &   # 抢走 CLIPBOARD owner 并等待粘贴
  ```
- **影响**：对 XWayland 内全部应用：键盘监听（凭据窃取）、XTEST 输入注入（冒充用户操作）、任意窗口截屏（GetImage）、剪贴板双向控制。是 X11 信任模型的结构性缺陷，但本实现连"限制到本 UID"的基本缓解（xauth/0600 socket）都没有做。
- **修复**：
  1. 生成每会话 xauth cookie（如 wlroots 做法），Xwayland 以 `-auth $XDG_RUNTIME_DIR/hyprland-xwayland.xauth` 启动；仅向本 UID 的环境导出 DISPLAY；
  2. socket 权限按归属收窄：本机场景 `chmod 0600` 即可（xhost 需求方由用户显式 `hyprctl` 放开），或至少 0660 + 专用组；
  3. 锁文件（`/tmp/.X<N>-lock`）内容泄露 PID 无关紧要但可辅助探测，考虑使用 `-displayfd` 结果不落公共目录。

---

## F-XW2-02 ｜WL_SURFACE_ID 客户端消息不校验 wl 对象接口 → 类型混淆/悬垂解引用；跨应用 surface 劫持 — 严重程度：**高**（T1 → 合成器内存安全 + 内容窃听）

- **位置**：`src/xwayland/XWM.cpp:403-414`（`handleClientMessage` 的 `WL_SURFACE_ID` 分支）；`src/protocols/core/Compositor.cpp:326-329`（`CWLSurfaceResource::fromResource`）。
- **描述**：X11 客户端可向**自己的** X 窗口发送 `WL_SURFACE_ID` client message，`e->data.data32[0]` 被原样当作 Xwayland（即 `m_xwaylandClient`）的 wl_resource id 使用：
  ```cpp
  auto id       = e->data.data32[0];                                   // X 客户端全控
  auto resource = wl_client_get_object(g_pXWayland->m_server->m_xwaylandClient, id);
  if (resource) {
      auto surf = CWLSurfaceResource::fromResource(resource);          // 无任何接口校验
      associate(XSURF, surf);
  }
  ```
  `fromResource` 盲目执行 `sc<CWlSurface*>(wl_resource_get_user_data(res))->data()`：若 id 指向 Xwayland 创建的**非 wl_surface** 对象（wl_buffer、xdg_output 桥接对象、shm、seat 代理等），user_data 不是 `CWLSurface*`，读其 `data()` 成员为野指针 → UB/崩溃；若恰好解析出其他 X 应用窗口的 `CWLSurfaceResource`，攻击者窗口被 `associate()` 绑定到**他人 surface**，其内容被贴上攻击者窗口并进入合成/输入路由（内容窃取、输入错位）。id 是服务端顺序分配的整数，暴力枚举可行（XWayland 客户端 id 空间小）。对比：`WL_SURFACE_SERIAL` 分支（415-436）要求攻击者知道 64 位 serial，实际不可伪造——说明作者知道该风险，id 遗留路径是缺口。
- **前置条件**：T1，普通 X11 客户端能力（`XSendEvent` 到自己窗口），无需 root。
- **PoC**（libxcb）：
  ```c
  /* 先 map 一个窗口拿到 XSURF；然后: */
  xcb_client_message_event_t ev = {0};
  ev.response_type = XCB_CLIENT_MESSAGE; ev.format = 32; ev.window = my_win;
  ev.type = atom("WL_SURFACE_ID");
  ev.data.data32[0] = candidate_id;            /* 1..N 扫描 */
  xcb_send_event(dpy, 0, my_win, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
  ```
- **影响**：合成器崩溃（T1 DoS）；潜在跨应用表面内容窃取/UAF。
- **修复**：校验 `wl_resource_instance_of(resource, &wl_surface_interface, ...)`（或等价：仅接受从 `m_shellResources`/`CWLSurfaceResource` 注册表反查得到的对象），拒绝任意 id；只信任 serial 匹配路径，逐步废弃 WL_SURFACE_ID。

---

## F-XW2-03 ｜XDataSource 构造读 _WL_SELECTION 只查 type 不查 format → 堆越界读（F-XW-01 同类新位置） — 严重程度：**中高**（T1/T3 → 崩溃或数据外带）

- **位置**：`src/xwayland/XDataSource.cpp:11-44`（第 19 行只检查 `reply->type != XCB_ATOM_ATOM`，第 25 行按 4 字节元素遍历 `reply->value_len` 项）。
- **描述**：`_WL_SELECTION` 属性位于 **WM 自建的 selection window**（`sel->window`）上。X11 中任何客户端都可对**任意**窗口调用 `XChangeProperty`。恶意 X 客户端无需等待 convert 流程，直接向 `m_clipboard.window` 的 `_WL_SELECTION` 写入 `type=ATOM, format=8, value_len=4096`（get_property 请求长度即 4096，`XDataSource.cpp:13`）：回复缓冲区仅 4096 字节，但循环 `value[i]` 以 4 字节 × 4096 读到 16KB → 越界堆读 ~12KB。触发点是 `handleSelectionNotify → setClipboardToWayland`（`XWM.cpp:683,1332-1334`）构造 `CXDataSource` 之时。越界字节还以 atom 值进入 `m_mimeTypes/m_mimeAtoms`，后续被 `mimeFromAtom` 回查。
- **前置条件**：T1 攻击者需让一次 TARGETS 转换发生（X 窗口获焦 + 自己或共谋进程持有 CLIPBOARD），或直接以正常途径成为 selection owner。
- **PoC**（libxcb；先 `xcb_get_atom("WL_SELECTION"/对应 atom)` 与 WM selection window id——可由攻击者抢先 `xcb_convert_selection` 触发 WM 侧流程，selection window id 可从 XFixes 事件中枚举）：
  ```c
  uint8_t junk[4096] = {0x41};
  xcb_change_property(dpy, XCB_PROP_MODE_REPLACE, wm_selection_window,
                      atom("_WL_SELECTION"), XCB_ATOM_ATOM /*type*/, 8 /*format!!*/, 4096, junk);
  /* 随后 acquire CLIPBOARD，WM 自动 convert TARGETS → XDataSource 构造 → OOB */
  ```
- **影响**：X11 客户端可稳定使合成器 SIGSEGV（比 F-XW-01 更隐蔽，位于选区子系统）。
- **修复**：加 `reply->format == 32` 且 `xcb_get_property_value_length(reply) >= value_len * 4` 双守卫；与 F-XW-01 的修复统一封装 `readAtomArrayProp()`。

---

## F-XW2-04 ｜handleTransientFor 未校验 format 且允许 value_len==0 → OOB 读 + 任意 parent 伪造（F-XW-01 同类新位置） — 严重程度：**中**（T1）

- **位置**：`src/xwayland/XWM.cpp:287-300`。
  ```cpp
  if (reply->type != XCB_ATOM_WINDOW) return;      // 缺 format == 32 校验
  const auto XID = rc<const xcb_window_t*>(value); // value 可能为 0 长度缓冲区
  ... windowForXID(*XID)                           // 解引用 4 字节
  ```
- **描述**：`readProp` 用 `XCB_ATOM_ANY`（`XWM.cpp:378`）取回属性。客户端为 `WM_TRANSIENT_FOR` 提交 `type=WINDOW, format=8, value_len=0`（或 format=8 + 任意短 value）：`value` 指向 0/1 字节的回复尾部，`*XID` 越界读取回复缓冲区之后的堆。虽然这里只解引用一次（不像 atoms 循环放大），仍是未定义行为，且读到的野值作为 parent XID 参与 `lookupParentExists`/`m_parent`/modal 归属——攻击者可借 `format` 混淆让自己的窗口挂到任意窗口 id 之下，影响模态/焦点跟随逻辑。`handleSizeHints`（302-335）与 `handleWMHints`（257-276）依赖 xcb-icccm 的内部长度校验，未见同类问题。
- **PoC**：
  ```c
  uint8_t b = 0;
  xcb_change_property(dpy, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_TRANSIENT_FOR,
                      XCB_ATOM_WINDOW, 8 /*format*/, 0 /*value_len*/, &b);
  /* 触发 PropertyNotify 即走 readProp */
  ```
- **修复**：`if (reply->format != 32 || xcb_get_property_value_length(reply) < 4) return;`

---

## F-XW2-05 ｜X→Wayland 剪贴板/PRIMARY 注入无节流、无锁屏门控；focus 判定用陈旧的 m_focusedSurface — 严重程度：**中**（T1/T3 → Wayland 应用粘贴内容注入；锁屏期间选区可被劫持）

- **位置**：
  - `src/xwayland/XWM.cpp:806-846`（`handleSelectionXFixesNotify`：任何 X 客户端 acquire CLIPBOARD/PRIMARY 即自动发起 TARGETS 转换，**无 isSessionLocked、无 focus、无速率限制**）；
  - `src/xwayland/XWM.cpp:677-683`（转换完成仅要求 `m_focusedSurface` 非空——它是上次 WM 焦点的 WP，`handleUnmapNotify`（157-172）与 withdraw 路径**不清空**它）；
  - `src/xwayland/XWM.cpp:483-484`（`_NET_ACTIVE_WINDOW` 自激活：任意 X 客户端对自己窗口发该消息 → `activate.emit()`，多数配置会给予焦点）；
  - 整个 `src/xwayland/` 对 `g_pSessionLockManager` **零引用**（grep 证实）。
- **描述**：组合链：攻击 X 应用 (a) 发 `_NET_ACTIVE_WINDOW` 夺取焦点（或只要历史上某个 X 窗口仍残留在 `m_focusedSurface` 里），(b) `XSetSelectionOwner(CLIPBOARD)` 并携带任意 mime/内容 → WM 自动 `setClipboardToWayland` → `g_pSeatManager->setCurrentSelection(CXDataSource)`。此后受害者在任何 Wayland 应用 Ctrl+V / **中键（primary 自动粘贴，终端类应用尤其危险）** 得到攻击者字节。无字符数/大小上限（`onRead` 每 chunk `resize` 追加，`XWM.cpp:1507-1533`；`getIncomingSelectionProp` 单次请求上限 0x1fffffff，`XWM.cpp:1731` → 大剪贴板 OOM DoS 同源）。锁屏期间该路径不被阻断：X 客户端可在锁屏时把 seat selection 换成自己内容，等解锁后第一次粘贴即中招（渲染与键盘虽被 `Renderer.cpp:1176-1181` / `FocusState.cpp:106,235` 挡住，但选区状态机照常运转）。
- **PoC**：
  ```c
  /* libxcb: 恶意 X app */
  xcb_client_message_event_t act = {.response_type=XCB_CLIENT_MESSAGE, .format=32,
      .window=win, .type=atom("_NET_ACTIVE_WINDOW")};
  act.data.data32[0]=2; xcb_send_event(dpy,0,root,XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,(char*)&act);
  xcb_set_selection_owner(dpy, win, atom("CLIPBOARD"), XCB_CURRENT_TIME);
  /* 作为 owner 应答 ConvertSelection: 返回 "rm -rf ~ #\n" 之类 text/plain */
  ```
  受害者在中键粘贴的 Wayland 终端里中键 → 命令注入。
- **修复**：(1) `handleSelectionXFixesNotify` 增加 `g_pSessionLockManager->isSessionLocked()` 拒绝 + X 侧焦点实时校验（对比 seat focus 而非陈旧 `m_focusedSurface`）；(2) 选区数据加总量上限与所有权切换节流（同窗口短时间内反复 acquire 降权）；(3) unmap/withdraw 时清空 `m_focusedSurface`。

---

## F-XW2-06 ｜WM_CLASS / _NET_WM_NAME 客户端自报 → windowrulev2 身份冒充（规则提权 + 钓鱼） — 严重程度：**中**（T1/T3）

- **位置**：`src/xwayland/XWM.cpp:220-243`（`handleWMClass`/`handleWMName`，内容全部来自客户端 `XChangeProperty`，直接进入 `XSURF->m_state.appid/title` → 窗口规则匹配键）。pid 侧：`src/xwayland/XSurface.cpp:9-40` 使用 XCB-RES `LOCAL_CLIENT_PID`（服务器从 socket 对端取得，不可伪造），**但 class/title 与 pid 无绑定校验**。
- **描述**：用户为受信任应用写的规则（`windowrulev2 = ... ,class:^bankclient$`、`,title:^Confirm transfer$`）可被任意 X11 客户端冒领：冒充 class 获得该规则的浮窗/无边框/置顶/工作区/焦点策略/动画豁免等特权；同时标题栏与任务栏显示假冒身份 → 高精度钓鱼窗口（覆盖/模仿银行、密码管理器对话框）。与 F-XW2-05 组合（借 `above` 类规则盖窗）构成完整 UI 伪造链。`general:modal_parent_blocking` 等依赖 `_NET_WM_STATE_MODAL` 的行为也可经伪造 parent（F-XW2-04）操纵。
- **PoC**：
  ```bash
  # 验证面（xdotool 级）：设 class 后映射窗口，观察命中规则
  xterm -name bankclient -class BankClient &     # 或 libxcb:
  # xcb_change_property(dpy, REPLACE, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
  #                     len, "\0bankclient\0BankClient\0")
  ```
- **修复**：对 X11 窗口：(1) 在 windowrule 文档与 `hyprctl` 输出中把 X11 class/title 标注为不可信来源，并支持 `matches,pid:`（利用已有的真实 `m_pid`）作为可信锚点；(2) 可选 `xwayland:trust_identity=false`（默认）时，对与声明 class 不符（RES pid 非白名单）的 privileged 规则（above/focus/monitor 类）降级；(3) 渲染 title/class 时加视觉警示（类似浏览器 EV 提示）。

---

## F-XW2-07 ｜XdndFinished/XdndStatus 不校验发送者身份 → 伪造 DnD 完成，Wayland 源误判成功 — 严重程度：**中低**（T1 → 拖放语义破坏，MOVE 场景可诱导源端删除文件）

- **位置**：`src/xwayland/XWM.cpp:485-506`（`handleClientMessage` 的 `XdndStatus`/`XdndFinished` 分支）。
- **描述**：两条分支只要求 `windowForXID(e->window)` 存在（**任意**映射 X 窗口）且 `m_dndDataOffers` 非空，不校验 `e->window == offer->m_xwaylandSurface->m_xID`、不校验 `data.data32[0] == m_dndSelection.window`（XDND 协议规定接收端回送时 source window 必须匹配）。用户正从 Wayland 文件管理器向某 X 应用拖拽 MOVE 时，恶意 X 进程（甚至只是碰巧映射的窗口进程）对自己窗口发 `XdndFinished` → `m_dndDataOffers.at(0)->getSource()->sendDndFinished()`（`XWM.cpp:504`）→ Wayland 源收到 success；对实现"成功后删除源文件"的剪贴板管理器/文件管理器，等于第三方触发文件删除确认路径。`Dnd.cpp` 的 `getProxyWindow`（43-72）已正确做了 XdndProxy 自引用校验（fork 的改进），但入站消息侧缺同源校验。DnD 文件路径（`text/uri-list`）由 X 侧 `CXDataSource` 提供（F-XW2-03 同对象），drop 进 Wayland 端即注入任意 URI 文本，无 `file://` 白名单/存在性检查——依赖接收应用自律。
- **PoC**（恶意 X 进程，与拖放目标无关也行）：
  ```c
  xcb_client_message_event_t fin = {.response_type=XCB_CLIENT_MESSAGE, .format=32,
      .window=my_win, .type=atom("XdndFinished")};
  fin.data.data32[0]=any; fin.data.data32[1]=1 /*accept*/; fin.data.data32[2]=atom("XdndActionMove");
  xcb_send_event(dpy,0,my_win,XCB_EVENT_MASK_NO_EVENT,(char*)&fin);
  ```
- **修复**：仅当 `XSURF == m_dndDataOffers[0]->m_xwaylandSurface` 且 `data.data32[0] == m_dndSelection.window` 时接受；否则丢弃并 WARN。

---

## F-XW2-08 ｜XWayland hangup 重启路径：~SXTransfer 解引用已 reset 的 m_wm → 崩溃循环（挂起资源未清理 + 生命周期错误） — 严重程度：**中低**（T1 配合可升级：合成器无法自愈重启）

- **位置**：`src/xwayland/XWM.cpp:869-881`（onEvent HANGUP → `doLater` 里 `g_pXWayland->m_wm.reset(); ...`）与 `src/xwayland/XWM.cpp:1720-1727`（`~SXTransfer`）。
- **描述**：`m_wm.reset()` 将 `unique_ptr` 置空后才销毁旧 `CXWM`；成员销毁顺序连带销毁 `m_clipboard/m_primarySelection` 的 `transfers` 向量。`~SXTransfer` 无条件访问 `g_pXWayland->m_wm->m_connection`（`xcb_destroy_window(*g_pXWayland->m_wm->m_connection, ...)`）——此刻 `m_wm == nullptr` → 空指针解引用。触发条件：XWayland 挂断（崩溃/被杀）时存在带 `incomingWindow` 的活跃传输（即 `CXDataSource::send` 创建的 X→Wayland 传输，`XDataSource.cpp:74-93`）。攻击者可先用 F-XW2-01/03 类原语击杀 Xwayland，同时保持一条进行中的 X 剪贴板传输，使"自动重启"变成"重启即崩"，把 X11 DoS 升级为合成器持久崩溃。另外 `die()`（`Server.cpp:280-285`）故意泄漏 `wl_client`（"possible crash. Better to leak a bit"），重启路径每次泄漏一个 wl client 及其 fd，反复 kill Xwayland 可积累泄漏。
- **PoC**：恶意 X app 发起大 INCR 剪贴板传输（ Wayland 端读得慢，管道保持非空）→ 同时另一进程用 X 服务器崩溃漏洞击杀 Xwayland → 观察 `g_pXWayland->m_wm.reset()` 后立刻 NULL 解引用崩溃。
- **修复**：重启 lambda 中先显式 `sel.transfers.clear()`（在 `m_wm` 尚有效时）或将 `~SXTransfer` 的销毁逻辑改为持有 `WP<CXWM>`/连接快照；`die()` 的 client 泄漏改为在旧 `CXWaylandServer` 完全析构后由事件循环安全销毁。

---

## F-XW2-09 ｜CreateNotify 无差别建窗口对象 + 每窗口多次同步 X 往返 → 窗口风暴放大 DoS — 严重程度：**中低**（T1 → CPU/内存放大）

- **位置**：`src/xwayland/XWM.cpp:56-66`（`handleCreate`：除 WM 窗口外每个 CreateNotify 都创建 `CXWaylandSurface` **和** `CWindow`）；`src/xwayland/XSurface.cpp:9-77`（构造函数内 3 次同步往返：`xcb_res_query_client_ids`、`xcb_change_window_attributes`，`recheckSupportedProps` 中 `xcb_list_properties` + `xcb_get_property`）。
- **描述**：恶意 X 客户端循环 `xcb_create_window`（unmapped、1×1、OR 均可）即让合成器事件循环为每个窗口做多次**阻塞式**同步请求并分配完整窗口栈（CWindow + XSurface + 监听器）。单进程每秒可造数千窗口，全部在合成器线程排队往返，饿死输入/渲染；且每个 XSurface 都注册 `FOCUS_CHANGE|PROPERTY_CHANGE` 事件掩码，进一步放大后续流量。这些窗口无需 map 即永久驻留 `m_surfaces`（销毁仅靠 DestroyNotify）。
- **PoC**：
  ```c
  for (uint64_t i=0;i<1000000;i++){
    xcb_window_t w = xcb_generate_id(dpy);
    xcb_create_window(dpy, XCB_COPY_FROM_PARENT, w, root, 0,0,1,1,0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, vis, 0, NULL);
    if (i%512==0) xcb_flush(dpy);
  }
  ```
- **影响**：可复现的输入延迟→假死（比崩溃更隐蔽：键鼠"卡住"）。
- **修复**：`handleCreate` 只登记有映射意图的窗口（延迟到 MapRequest/CreateNotify+子结构掩码），或维护轻量 xid 表；把 `recheckSupportedProps` 的往返改异步（cookie 批处理）；对未映射窗口数量设每客户端上限。

---

## F-XW2-10 ｜/tmp lock 文件全局命名空间：同机用户可阻死 XWayland 启动（DISPLAY DoS/劫持竞态） — 严重程度：**低**（T2 → XWayland 不可用/错误归属）

- **位置**：`src/xwayland/Server.cpp:195-249`（`tryOpenSockets`）。
- **描述**：display 分配走 X11 传统 `/tmp/.X<N>-lock`（O_EXCL，世界可写 /tmp 内的全局名字空间）。攻击者（T2）预先 `touch /tmp/.X0-lock` 并写入**自己存活进程**的 11 字节 pid：Hyprland 走 `kill(pid,0)==0` 分支跳过该 display；对 0..31 全部占位 → `Failed to find a suitable socket`，XWayland 整体不可用（所有 X 应用挂掉）。若攻击者进程随后退出（或写死进程 pid 骗过 ESRCH 检查后清理），存在锁文件删除/重建竞态：另一实例可抢占同一 display 编号，两个 server 争夺同一 socket（bind 时 `unlink` 先行，`Server.cpp:53-54` 会**删除他人 socket 文件**）→ 客户端连错 server/被 MITM（配合 F-XW2-01 无认证，攻击者自建 `:0` 监听即可收编受害者 X 应用连接）。
- **PoC**：
  ```bash
  # eviluser:
  for i in $(seq 0 31); do printf '%010d\n' $$ > /tmp/.X$i-lock; done; sleep 999999 &
  # 受害者重启 Hyprland → XWayland 起不来
  ```
- **修复**：优先使用 `-displayfd` + 私有目录（`XDG_RUNTIME_DIR`）分配 display，忽略 /tmp lock 的存在性竞争（或检测到占用者 uid≠self 时仅 WARN 并尝试 abstract socket 兜底）；bind 前校验 `sun_path` 现有文件 owner==self 才 unlink。

---

## F-XW2-11 ｜杂项低危（合并记录） — 严重程度：**低**

1. **选区时间校验可绕过**：`XWM.cpp:749-753` 的过期请求拒绝在 `e->time == XCB_CURRENT_TIME` 时整体跳过，且不验证 `e->requestor` 是否请求者本人 → owner 切换竞态下可读到新 owner 内容（X11 无隔离模型下属弱点强化，配合 F-XW2-05 节流缺失）。
2. **Xwayland 经 `/bin/sh -c` + PATH 解析启动**：`Server.cpp:318` `execl("/bin/sh","/bin/sh","-c","exec Xwayland ...")`。字符串本身无注入（display/fd 均内部生成），但 `Xwayland` 二进制依赖调用环境的 PATH；若部署环境 PATH 含用户可写目录则被劫持。建议直接 `execvp` 解析后的绝对路径（`FsUtils::executableExistsInPath` 已在 `XWayland.cpp:21` 拿到绝对路径，却未传给 exec）。
3. **`runXWayland` 失败路径 `_exit(EXIT_FAILURE)`**（`Server.cpp:307,321`）：fork 子进程在 cloexec 失败时 `_exit`，父进程无 waitpid/无重试 → 挂起僵尸 + XWayland 静默死亡，无恢复（与 `ready()` 失败仅 `m_server.reset()`，`Server.cpp:410-413` 一致——**本 fork 无 `onXWaylandCrashRequest`/lazy 重启机制**，X 应用崩溃后仅 hangup 路径有重启，见 F-XW2-08）。
4. **`handleWMHints` 静默丢弃超长 WM_HINTS**（`XWM.cpp:266-267`）：`value_len > XCB_ICCCM_NUM_WM_HINTS_ELEMENTS` 直接 return，旧 hints 保留——攻击者可用超长非法值**冻结** hint 更新（锁定 INPUT hint 欺骗 focus-for 判定），属正确性弱点非内存问题。

---

## 锁屏链路结论（任务 5 专项）

- **已挡住**：workspace 渲染（含 X11 常规与 override-redirect 窗口，`Renderer.cpp:1176-1181`，默认 `misc:session_lock_xray=0`，`ConfigValues.cpp:583`）；键盘焦点授予（`FocusState.cpp:106-109`）；seat 键盘焦点切换（`FocusState.cpp:235`）。X 客户端 `XGrabKeyboard` 只作用于 XWayland 内部焦点，键事件入口在 Hyprland seat，无法绕过。
- **未挡住（本报告 F-XW2-05/08/09 的锁定态变体）**：`src/xwayland/` 全包 **0 处** sessionLock 检查——X 客户端在锁屏期间仍可：创建/映射窗口并占用资源（解锁瞬间呈现抢跑 UI）、切换 CLIPBOARD/PRIMARY owner 改写 Wayland seat 选区（解锁后第一粘贴中招）、窗口风暴拖慢锁屏帧循环。`session_lock_xray=1` 的部署下上述全部升级为可见攻击（X 覆盖锁屏需 OR 窗口进入 layer 渲染路径，未发现 OR→overlay 提层，但 xray 下 workspace 照常渲染即为已知风险配置）。
- **建议**：在 `CXWM::onEvent`、`handleSelectionXFixesNotify`、`handleMapRequest`、`CX11DataDevice::sendEnter/sendDrop` 增加统一 `g_pSessionLockManager->isSessionLocked()` 门控（延迟处理而非丢弃，解锁后重放），并补 `hyprtester` 锁屏用例。

## 汇总

| ID | 标题 | 严重程度 | 关键位置 |
|---|---|---|---|
| F-XW2-01 | 0666 socket + 无 xauth：同机用户全权访问 DISPLAY | 高 | Server.cpp:66, 310 |
| F-XW2-02 | WL_SURFACE_ID 任意 id → 类型混淆/surface 劫持 | 高 | XWM.cpp:403-414 |
| F-XW2-03 | _WL_SELECTION format 混淆堆 OOB（新位置） | 中高 | XDataSource.cpp:19-25 |
| F-XW2-04 | WM_TRANSIENT_FOR format 混淆/零长度解引用 | 中 | XWM.cpp:287-300 |
| F-XW2-05 | X→Wayland 选区注入无节流/无锁屏门控 | 中 | XWM.cpp:806-846, 677-683 |
| F-XW2-06 | WM_CLASS/标题冒充 → windowrule 冒领/钓鱼 | 中 | XWM.cpp:220-243 |
| F-XW2-07 | 伪造 XdndFinished 破坏 DnD 语义 | 中低 | XWM.cpp:485-506 |
| F-XW2-08 | hangup 重启 ~SXTransfer 空指针（含 wl_client 泄漏） | 中低 | XWM.cpp:869-881, 1720-1727 |
| F-XW2-09 | CreateNotify 窗口风暴同步往返放大 DoS | 中低 | XWM.cpp:56-66; XSurface.cpp:9-77 |
| F-XW2-10 | /tmp/.X-lock 全局命名空间抢占/DoS | 低 | Server.cpp:195-249 |
| F-XW2-11 | 杂项（时间校验绕过/PATH exec/僵尸/ hint 冻结） | 低 | 见各小节 |

---

## 验证状态（2026-10-15，本机尝试记录）

本机尝试用 `weston --backend=headless` + 树内 `build/Hyprland`（wayland backend）起嵌套会话验证 F-XW2-01/02：
**失败**，根因与漏洞无关——本环境无 GPU（无 `/dev/dri`），weston headless 不导出 `zwp_linux_dmabuf_v1`，
Aquamarine Wayland backend 启动检查即 `Wayland backend cannot start: zwp_linux_dmabuf_v1 init failed`，
`CBackend::create()` 返回 null（`Compositor.cpp:326-332`）。静态证据本身已复核：

- `chmod(addr->sun_path, 0666)`：`Server.cpp:66`（注释自述依赖 Xwayland 的 xhost ACL）；
- Xwayland 命令行无 `-auth`：`Server.cpp:310-311`（仅 `-rootless -core -listenfd×2 -displayfd -wm`）。

### 待有 GPU/正常会话的主机执行的最小验证

1. `stat -c %a /tmp/.X11-unix/X<N>`（Hyprland 会话内）→ 期望 `666`；
2. `cat /proc/$(pgrep Xwayland)/cmdline` → 期望无 `-auth` 参数；
3. 第二用户：`sudo -u victim2 DISPLAY=:<N> xdotool getdisplaygeometry`；
   - 成功 = xhost ACL 未拦 → 继续 `DISPLAY=:<N> xinput test-xi2`（键监）与 `xsel -b`（剪贴板）坐实；
   - 被拒 = 默认 ACL 生效，F-XW2-01 降级为"仅当用户自行 xhost +"的配置性风险，请在此文档更新结论；
4. F-XW2-02：X 客户端向 WM_DYING window 发 ClientMessage(type=XWAYLAND_SURFACE_ID, data[0]=伪造 id) 观察崩溃/劫持。
