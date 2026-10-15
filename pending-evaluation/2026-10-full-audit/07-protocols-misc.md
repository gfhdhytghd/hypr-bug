# 07 — 杂项 Wayland 协议实现审计（screencopy / session lock / vk / text-input / IME / dmabuf / drm-lease / pointer-constraints / idle-inhibit / gamma / hyprland-*）

> 仓库：/home/wilf/data/Hyprland（Hyprland v0.56.0 fork，HEAD 397cb0efb）
> 威胁模型：任意可连接 socket 的恶意 Wayland 客户端。除特殊说明外，所列全局均由 `src/managers/ProtocolManager.cpp:173-258` 无条件向所有客户端广播（无 SecurityContext/信任门控）。
> fork 改动（44a651642 CM-issue-fix）重点复查了 ColorManagement/GammaControl，见 F-PR2-10 与文末附注。

---

## F-PR2-01 — virtual-keyboard keymap：客户端控制 len/fd 未校验 → mmap 越界读/SIGBUS 合成器崩溃

- **严重程度**：高（远程客户端一行请求杀死整个合成器）
- **位置**：`src/protocols/VirtualKeyboard.cpp:74-108`（mmap 在 :83，解析在 :91）；全局广播 `src/managers/ProtocolManager.cpp:191`
- **攻击路径**：
  1. bind `zwp_virtual_keyboard_manager_v1`（无权限要求），`create_virtual_keyboard(seat)`；
  2. 发送 `keymap(format, fd, len)`：fd 指向一个 1 字节的 memfd，`len` 声称 0x7FFFFFFF；
  3. 实现直接 `mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd)` —— **没有 fstat 校验 fd 实际大小**；
  4. `xkb_keymap_new_from_string` 按 C 字符串读取映射区：越过 fd 实际 EOF 的页在首次访问时触发 **SIGBUS**；且若内容无 NUL 终止符，读取越过映射尾部同样 SIGBUS。`src/` 全树 grep 无任何 SIGBUS/SIGSEGV 处理器（崩溃即进程终止）。
  5. 附带：`len` 无上限，巨型 keymap 字符串在 xkb 编译期可放大内存占用；每次调用 `xkb_context_new`（:75）无速率限制。
- **PoC 思路**：`memfd_create; write(fd,"x",1); wl_send_keymap(fmt, fd, 0x40000000)` → 合成器收到 SIGBUS 崩溃，所有窗口丢失。
- **影响**：整机图形会话 DoS。
- **修复建议**：`fstat` 校验 `st_size >= len` 且 `len` 设上限（如 1MB）；以 `len` 精确解析（`xkb_keymap_new_from_bytes`）而非按 C 字符串读取；确保映射区内强制 NUL 或按长度拷贝。

---

## F-PR2-02 — input-method-v2：首个 bind 者独占“唯一 IME”，无信任校验 → 全局键盘输入注入 + surroundingText 窃密 + 挤掉真 IME

- **严重程度**：高
- **位置**：
  - `src/managers/input/InputMethodRelay.cpp:18-30`（第一个创建 IME 资源的客户端直接成为 `m_inputMethod`；后来者仅收 `unavailable()`）
  - `src/protocols/InputMethodV2.cpp:163-176`（`commit_string`/`set_preedit_string`/`delete_surrounding_text` 任意字符串，无校验）、:181-185（`commit(serial)` 的 serial **完全被忽略**，不校验 done 序列）
  - `src/managers/input/TextInput.cpp:236-247`（焦点应用的 `surrounding_text` 原文转发给被劫持的 IME）、:260-295（IME 的 commit/preedit 原样注入焦点文本框）
  - `src/protocols/InputMethodV2.cpp:11-69`（`grab_keyboard` 向攻击者客户端发送 keymap/key/modifiers，可被动监听实体键盘按键事件）
  - 全局广播：`src/managers/ProtocolManager.cpp:190`
- **攻击路径**：
  1. 恶意客户端开机即 bind `zwp_input_method_manager_v2` 并 `get_input_method` → 抢注为唯一 IME（真实 fcitx/ibus 此后永远收到 `unavailable`，IME 拒绝服务）；
  2. 任意应用（含浏览器、终端）获得键盘焦点并被 text-input 激活后，其 `set_surrounding_text` 内容、光标位置被送到攻击者（窃密信道；是否泄露取决于应用是否对密码框上报 surrounding text，且 v1 的 purpose 转换本身有缺陷，见 F-PR2-07）；
  3. 攻击者 `commit_string` → 合成器无条件写入焦点应用（向 polkit/锁屏/浏览器地址栏注入文本）；`grab_keyboard` 进一步被动监听按键。
- **PoC 思路**：weston-keyboard 改造：bind IME → 收到 activate 时打印 surrounding_text；commit_string 定时注入。
- **影响**：输入注入 + 潜在密码/内容泄露 + IME 生态 DoS。
- **修复建议**：对 IME 全局做信任门控（仅授权进程可 bind，或按 XDG_CURRENT_DESKTOP/环境白名单）；校验 `commit` serial 与 `done` 序号配对；对 commit/preedit/surrounding 字符串设长度上限与速率限制；`grab_keyboard` 事件流在锁屏期间必须静默。

---

## F-PR2-03 — linux-dmabuf：plane fd 实际尺寸从不校验 → GPU 越界访问/驱动挂死 DoS（跨客户端显存读取风险）

- **严重程度**：中高
- **位置**：`src/protocols/LinuxDMABUF.cpp:148-179`（add：stride/offset 原样存储）、:318-327（verify：**仅**检查 `offset + stride*height` 溢出 UINT32_MAX，不与 fd 实际大小比对）、:268-287（commence：只做 `drmPrimeFDToHandle` 有效性探测，导入即关闭 handle，不校验尺寸）、`src/protocols/types/DMABuffer.cpp:22-43`（用客户端属性创建 EGLImage/texture）
- **攻击路径**：
  1. 攻击者自行打开 render node，分配一个 4×4 的小 dmabuf；
  2. `zwp_linux_buffer_params_v1.create_immed(w=20000,h=20000,fmt,...)` + `add(fd=小buf, plane=0, offset=接近UINT32_MAX-stride*h 内, stride=32768)` —— 乘积仍在 uint32 范围内，verify 通过；
  3. EGL 导入携带越界 offset/stride 的平面并被渲染采样 → GPU 页表越界访问：amdgpu/i915 触发 GPUVM/GTT page fault → 引擎重置（全屏闪烁/卡死，持续重复即稳定 DoS），驱动不严格校验时甚至可读到其它进程 BO 的内容（信息泄露）。
- **PoC 思路**：libgbm 分配 1 页 BO，`create_immed` 谎报大尺寸高 stride，commit 到子表面，循环提交。
- **影响**：GPU 挂死级 DoS；潜在跨客户端显存泄露。
- **修复建议**：verify() 中对每个 plane fd `fstat`/`drmPrimeFDToHandle+gem_close` 后查询 `drmGetFdBuffer` 或用 `fstat.st_size`（dmabuf 的 st_size 即 BO 大小）校验 `offset + stride*height(按平面 subsampling 折算) <= st_size`；失败按 OUT_OF_BOUNDS 报错。

---

## F-PR2-04 — wl_shm pool：客户端可在 mmap 后缩小 backing file → 合成器写/读页 SIGBUS；resize 失败后 MAP_FAILED 未失效化 → 野指针

- **严重程度**：高
- **位置**：`src/protocols/core/Shm.cpp:81-83`（pool 构造 `MAP_SHARED` 长生命周期映射）、:90-101（`resize`：先 `munmap`，mmap 失败仅 LOG，`m_data` 停在 `MAP_FAILED` 即 `(void*)-1`）、:65-66（`beginDataPtr` 直接 `m_data + m_offset` 算术，不检查 MAP_FAILED）、:138-156（create_buffer 只在创建时校验）；触发写侧：`src/managers/screenshare/ScreenshareFrame.cpp:481-484`（`readPixels` 惰性写客户端 pool 页）。全树无 SIGBUS 处理器。
- **攻击路径**：
  1. 创建 pool（memfd 16MB，通过 `shmIsSizeValid` 的 fstat 校验，pool 被 `MAP_SHARED` 映射）并创建 buffer；
  2. 向恶意 screencopy 提交 copy（合成器稍后在帧回调里才 `readPixels` 写该 pool），随后立即 `ftruncate(fd, 0)`；
  3. 合成器在损坏帧写入未 fault-in 的页时命中已截断的文件范围 → **SIGBUS → 合成器进程死亡**。
  4. 变体：resize 校验通过后 mmap 失败（fd 竞态缩小 / VMA 耗尽），`m_data=MAP_FAILED`，但既有 buffer 的 `beginDataPtr` 返回 `(-1)+offset` 附近野指针，读写即 SIGSEGV。
- **PoC 思路**：grim 式客户端，copy() 后 `ftruncate(0)`，等合成器写帧。
- **影响**：任何使用 shm 的写路径（screencopy、cursor、截图）都成为崩溃扳机；全协议面 DoS。
- **修复建议**：pool mmap 前用 `F_SEAL_WRITE`/要求 memfd 密封不可行时，至少安装 SIGBUS 保护（sigsetjmp 包裹像素拷贝，参考 wl_shm SIGBUS 惯用法）；`resize` 失败时将 pool 标记不可用并禁止既有 buffer 的 `beginDataPtr`；对 create_pool 使用 `MAP_FIXED_NOREPLACE` 无关但需捕获截断。

---

## F-PR2-05 — screencopy `capture_output_region`：捕获框完全信任客户端坐标，未与显示器范围求交 → 服务端巨型 FB 分配 OOM DoS + stride 计算整数溢出（UB）

- **严重程度**：中高
- **位置**：
  - `src/protocols/Screencopy.cpp:21-22`（`x,y,w,h` 原样进 `CBox`）、:92-93（`stride = minStride(PSHMINFO, bufSize.x)`，client 控制的 `bufSize.x` 参与 `width*bpp`，int32 溢出为 UB）
  - `src/managers/screenshare/ScreenshareManager.cpp:110-143`（getManagedSession 不做任何 clamp）
  - `src/managers/screenshare/ScreenshareSession.cpp:138-141`（`m_bufferSize = m_captureBox.size()`，甚至先按 scale 放大，:100-102 处 `scale(m_scale).round()`）
  - `src/managers/screenshare/ScreenshareFrame.cpp:453-455`（copyShm：`outFB->alloc(m_bufferSize.x, m_bufferSize.y, ...)` 服务端按客户端尺寸分配 CPU framebuffer）
- **攻击路径**：`capture_output_region(frame, 0, output, 0,0, INT32_MAX, INT32_MAX)` → sendBuffer 报告巨幅尺寸；客户端用稀疏 memfd 构造满足 `buffer->size == m_bufferSize` 与池校验的"假"巨型 shm buffer（`Shm.cpp:103-113` 的 fstat 对 sparse memfd 通过），提交 copy → 合成器 `alloc(2^31, 2^31)` / `setViewport` → OOM 或 huge allocation 崩溃；反复进行触发全局内存耗尽。负 w/h 未拒绝，产生负尺寸进入区域计算。
- **影响**：合成器 OOM 崩溃 / 渲染状态破坏；每客户端还可无限创建新 region session（不同 box 即新会话，`m_sessions` 无上限）。
- **修复建议**：`CScreenshareSession` 构造时将 captureBox 与 `PMONITOR->m_size` 求交，空交则拒绝；w/h 上限（如 8192×8192）；`minStride` 用 64 位中间量；每客户端 session/frame 数量限额。

---

## F-PR2-06 — 锁屏截屏竞态信息泄漏：screencopy/imageCopyCapture 无任何 `isSessionLocked` 门控；锁定后到 `locked` 事件前（最长约 5s）镜像纹理持续更新，锁定后仍可取回桌面最后帧

- **严重程度**：中
- **位置**：
  - `src/render/Renderer.cpp:1176-1180`（工作区仅在 `shallConsiderLockMissing() || clientLocked() || clientDenied()` 后停止渲染；`clientLocked` 需锁屏客户端为每输出渲染完成或 5 秒超时 —— `src/managers/SessionLockManager.cpp:113-131`）
  - `src/managers/screenshare/ScreenshareFrame.cpp:184-220`（renderMonitor 直接采样 `getMirrorTexture()`，即最后合成帧；`src/managers/screenshare/` 全目录 grep 无 sessionLock 引用）
  - 对照：wlroots 规范要求锁定时禁止共享（此处未实施）
- **攻击路径**：
  1. 恶意客户端预先 bind screencopy、开启 output 会话并循环 `copy()`（wlroots 客户端允许 pending frame 排队）；
  2. 用户锁屏瞬间，锁屏覆盖显示，但合成器为等待锁屏客户端首帧（慢客户端/崩溃客户端场景，`sendLockedTimer` 5s）继续渲染真实工作区并更新镜像纹理 → 攻击者在这段窗口内**持续收到锁屏前的实时桌面**；
  3. 即便 `locked` 后，攻击者新建 capture_output+copy 仍可取回冻结的桌面最后一帧（文档、终端内容等），而不是仅锁屏画面。
- **PoC 思路**：wlroots 系 grim 循环抓帧脚本 + 触发锁屏，比对帧内容与屏幕显示。
- **修复建议**：`getManagedSession`/`share()`/`copy()` 入口检查 `g_pSessionLockManager->isSessionLocked()`：锁定时 fail 新帧且将镜像纹理置黑/失效；锁定时清空 pending 已复制内容。

---

## F-PR2-07 — text-input v1 转发路径：`delete_surrounding_text` 无符号下溢 + purpose 转换缺陷放大 F-PR2-02

- **严重程度**：中
- **位置**：
  - `src/managers/input/TextInput.cpp:290-291`：`length() - ime->m_current.deleteSurrounding.before`，`before` 为攻击者（IME 客户端）任选 uint32，preedit 为空时 `0 - before` 下溢为 ~4.29e9，作为 `index`/`length` 发给受害 v1 客户端（`TextInputV1.cpp:92-94` 原样 send）
  - `src/protocols/TextInputV1.cpp:41`：v1→v3 purpose 映射 `purpose > PASSWORD ? hint + 1 : hint`，把 **hint 写进 purpose**，PASSWORD 语义丢失 → 依赖 purpose 做敏感过滤的 IME 侧防护失效
  - `src/protocols/TextInputV1.cpp:46-49`：客户端可任选 `serial_` 覆盖 `m_serial`，之后服务端 commit 事件用其做配对（`preeditString(m_v1Input->m_serial,...)` :279/287），serial 校验语义被污染
- **攻击路径**：恶意 IME（经 F-PR2-02 抢注）`delete_surrounding_text(before=0xFFFFFFFF, after=0)` + `commit` → 受害编辑器收到巨量删除参数；实现不严的客户端（以长度为 memcpy/erase 参数）崩溃或越界；purpose 缺陷使密码框上下文被按普通文本处理。
- **修复建议**：删除前做饱和运算/与 preedit 长度取 min；修正 v1→v3 purpose 映射；v1 serial 只由服务端递增分配，拒绝客户端自报。

---

## F-PR2-08 — drm-lease：向所有客户端广播且无锁屏/电源状态门控；lease 授予成功后监视器脱离合成器 KMS 控制

- **严重程度**：中
- **位置**：`src/protocols/DRMLease.cpp:22-91`（`CDRMLeaseResource` 构造：仅检查 `m_isBeingLeased`，随后 `Aquamarine::CDRMLease::create(outputs)` 直接 `sendLeaseFd`；无 `isSessionLocked()`、无 DPMS-off/未使用判定），`src/managers/ProtocolManager.cpp:236-243`（DRM 后端存在即对所有客户端注册 lease device）
- **攻击路径**：
  1. 主屏幕正在扫描输出时内核 lease 通常 EBUSY（缓解因素）；但被关闭/未使用（`m_enabled=false`、DPMS off、workspace 空置后电源超时）的连接器可被成功 lease → 攻击者获得独立 KMS master lease，在**锁屏期间**对该输出任意 scanout（伪造解锁提示、黑屏、闪烁）；
  2. `create_lease_request` 对象与 `m_leases` 无数量上限，配合反复请求逼近内核 `drm_lease` 句柄上限，干扰合法 VR/XR 租赁。
- **PoC 思路**：hyprctl dpms off 某屏 → 恶意客户端遍历 drm lease device → lease 该 connector → drmModeSetCrtc 输出图案。
- **修复建议**：锁屏时冻结新 lease（或至少拒绝）；只允许 lease 当前非活动连接器并给出用户提示；每客户端 lease 数量上限；考虑将全局仅暴露给已授权客户端。

---

## F-PR2-09 — 多协议 per-client 对象/副作用无配额：idle 抑制、虚拟键盘、screencopy 帧、IME 弹窗/grab、锁屏面均可无限创建

- **严重程度**：低（累积型资源耗尽 + 电源策略破坏）
- **位置**：
  - `src/protocols/IdleInhibit.cpp:46-52`：每客户端可绑 N 个 surface × N 个 inhibitor，`m_inhibitors` 无上限；不销毁即**永久阻止 DPMS/屏保**（配合 F-PR2-05 的廉价 surface）
  - `src/protocols/VirtualKeyboard.cpp:164-177`：`m_keyboards` 无上限，每个都成为 seat 键盘设备；高频 `key()`（:41-58）无速率限制 → 事件洪泛全体客户端；`m_pressed` 无界增长（重复 press 不同 keycode 直至 512 项/键码空间）
  - `src/protocols/Screencopy.cpp:39-51`：`m_frames` 与 `src/managers/screenshare/ScreenshareManager.cpp` 的 `m_pendingFrames` 在客户端不 destroy frame 时单调增长（仅客户端断开才清理，:191-193）
  - `src/protocols/InputMethodV2.cpp:186-215`：`m_popups`/`m_grabs` 无上限
  - `src/protocols/SessionLock.cpp:226-233`：单监视器 `m_lockSurfaces` 可重复创建多个
- **攻击路径**：单客户端循环上述请求且从不 destroy → 服务端容器/列表与每帧元数据线性增长；idle-inhibit 单独即构成"永不熄屏"。
- **修复建议**：引入 per-client 资源计数上限（wl_client 粒度），超限以协议错误回收；idle inhibitor 数量/时长审计。

---

## F-PR2-10 — gamma-control / hyprland CTM：锁屏期间仍可任意改写显示器 gamma/CTM → 视觉层拒绝服务（黑屏使人无法看到锁屏 UI）

- **严重程度**：中
- **位置**：`src/protocols/GammaControl.cpp:49-121`（`set_gamma` 读 fd 校验完备——单次 read 全表 + 1 字节探针 :80-99，防了溢出/超长，这点实现良好；问题在于**没有任何锁定态检查**），`src/protocols/CTMControl.cpp:35-62`（同样无锁检查；矩阵元素有 `isfinite`/`>=0` 校验，:39，良好）；`src/protocols/HyprlandSurface.cpp:28-34`、`AlphaModifier.cpp:26-30`、`BackgroundEffect.cpp`、`CommitTiming.cpp` 复查：数值均有范围校验、无 fd/mmap 面，未见独立可利用缺陷。
- **攻击路径**：恶意客户端 bind `zwlr_gamma_control_manager_v1`（全局无门控，ProtocolManager.cpp:177），在**锁屏发生前**绑定 gamma controller 并静置（`m_gammaTableSet=false` 时析构/应用均为 reset，故需持连接）；等待用户锁屏 → `set_gamma` 全 0 ramp（或 CTM 全 0 矩阵）→ 屏幕全黑/全白，用户无法看到锁屏输入界面；资源由攻击者持有直至其进程退出（析构 :129-135 才复位）。不改协议错误、无检测与日志。
- **PoC 思路**：wlgammagrab 改造：bind 后 idle，监视锁屏事件（LockNotify 全局可 bind，`src/protocols/LockNotify.cpp`）→ 触发黑 ramp。
- **修复建议**：`isSessionLocked()` 时拒绝 `set_gamma`/`set_ctm`（sendFailed），或锁定时强制清除客户端 ramp/CTM；锁屏事件到达时对所有 controller 做一次性复位。

---

## 附注（fork 改动复核，不构成独立发现）

- **GammaControl 的 fork 改动**（`git diff e2ad066c9..44a651642 -- src/protocols/GammaControl.cpp`）：仅把 `state->setGammaLut` 换成 `setClientGammaRamp`（与 ICC/CM 内部 LUT 分层协作）并新增 `gammaRampsInUse()` 拒改（:59-63）。未见新引入的校验缺失。
- **ColorManagement（fork 大改，44a651642）**：parametric 路径 luminance/primaries 有范围校验（:530、:540、:641、:674），transfer-function/primaries 枚举有白名单；`create_icc_creator` 已被显式拒绝（:135-136，`CColorManagementIccCreator` 成为死代码——注意其 `setSetIccFile` :478-482 若未来重新接线，存在**原始 fd 未持有所有权、未关闭、未 fstat** 的隐患，建议先行加固）；ICC 回传路径（:791-808）为服务端自读文件，非客户端可控。
- **SessionLock 本体**：`setGrab` 拒绝（SeatManager.cpp:755-758）与键盘焦点强制（InputManager.cpp:340-365）符合预期；`onGetLockSurface`（SessionLock.cpp:203-236）未限制 surface 角色复用（同一 wl_surface 可再被用作 xdg toplevel），但触发者只能是锁屏客户端自身，未见跨客户端提权路径。`unlockAndDestroy` 仅限锁的属主，未见第三方解锁路径。

### 优先级修复建议排序
F-PR2-01 / F-PR2-04（两个必崩 DoS，改动最小）→ F-PR2-02（信任模型，需产品决策）→ F-PR2-03 / F-PR2-05 → F-PR2-06 / F-PR2-10 → 其余加固。
