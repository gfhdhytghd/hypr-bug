# 审计域 14：DynamicPermissionManager / AsyncDialogBox / TokenManager（自主审计）

范围：`src/managers/permissions/`、`src/helpers/AsyncDialogBox.cpp`、`src/managers/TokenManager.cpp`、`src/config/supplementary/executor/Executor.cpp`（fork 路径）。
威胁模型：恶意 Wayland/X11 客户端、恶意同用户进程、被污染的 PATH。
背景：`ecosystem:enforce_permissions=1` 时才有权限弹窗；**默认 0 = 全部直接 ALLOW**（F-PERM-05）。

---

## F-PERM-01 ｜权限对话框结果 = 子进程裸 stdout，且按 PATH 解析定位二进制 → 提示可被"自动同意" — 严重程度：中（fail-open，配合环境可升级为权限系统整体旁路）

- **位置**：`src/helpers/AsyncDialogBox.cpp:15-18`（`executableExistsInPath("hyprland-dialog")`，PATH 解析）、`:93-139`（`open()` 用 `CProcess("hyprland-dialog", ...)`）、`:60-94`（`onWrite` 把子进程 stdout 原样累进 `m_stdout`）、`:84`（hangup 时 `m_promiseResolver->resolve(m_stdout)`）；消费端 `src/managers/permissions/DynamicPermissionManager.cpp:299-312`（`result.starts_with(ALLOW_ONCE/DENY/...)` 判用户选择）。
- **描述**：用户对 screencopy/plugin/keyboard/input-capture 的"批准"，其可信来源只是 **PATH 上解析到的 `hyprland-dialog` 可执行文件打印的字符串**。两个后果：
  1. 若 PATH 中靠前目录对攻击者可写（共享机器、`~/bin` 类用户目录、由启动器污染的 env），植入同名恶意二进制即可让**每一次 ask 直接回 "Allow"**（甚至 ALLOW_AND_REMEMBER 持久化 `m_binaryPath`）——人在环节被整体移除，`enforce_permissions` 形同虚设；
  2. 子进程异常退出（被 kill、崩溃、故意静默）时 promise 以**空串** resolve（非 error 分支），落入"所有 starts_with 都不匹配"路径 → 规则停在 PENDING：screencopy 客户端永久悬挂（软 DoS），且无超时回收。
- **攻击路径**：恶意应用触发权限弹窗（如反复请求 screencopy）→ 由被仿冒的 dialog 自动"点允许"→ 静默获得截屏/输入捕获权限。
- **修复**：`hyprland-dialog` 用固定安装路径+校验（不查 PATH）；结果协议用带 nonce 的握手（compositor 生成随机 token，dialog 须回 `token:result`），杜绝继承/伪造；空/异常结果一律按 DENY 收敛。

## F-PERM-02 ｜`spawnRawProc` fork 后不清理 fd → 合成器全部非 CLOEXEC fd 泄给每个 exec 出的应用 — 严重程度：中高

- **位置**：`src/config/supplementary/executor/Executor.cpp:178-207`（fork 后仅 dup2 stdout/stderr 到 /dev/null，**无 `close_range(3, UINT_MAX, 0)`/无 /proc/self/fd 遍历**，直接 `execl("/bin/sh","-c",...)`）；泄漏面（非 CLOEXEC 创建点）：
  - `src/helpers/AsyncDialogBox.cpp:106-109`：裸 `pipe(outPipe)`，read 端由 CAsyncDialogBox 持有整个对话框生命周期；
  - `src/xwayland/Server.cpp:42`（`socket(AF_UNIX, SOCK_STREAM, 0)`，0666 的 X listen socket 同族）、`:327`、`:342`（`socketpair(AF_UNIX, SOCK_STREAM, 0, ...)`）、`:367`（`pipe(notify)`）。
- **描述**：每个 `exec-once`/键绑定 `exec`/IPC eval 起的进程都继承这些 fd。具体可利用点：
  1. **对话框偷跑**：弹窗存在期间启动的任何应用（exec 由用户/脚本随时触发）继承 `outPipe[0]` 读端——可与合成器竞争 `read()` 抢走用户点击的字节（把批准"喂狗"，规则卡 PENDING，破坏 hangup→resolve 语义）；配合 F-PERM-01 的反向操作即可操纵权限结果。
  2. **X/Wayland 通道混接**：继承 `m_xFDs/wlPair/xwmPair` 端点的第三方进程可与 Xwayland 的私有 socketpair 通道竞争读写（Xwayland 崩溃重启窗口内尤甚），notify pipe 的写端泄漏意味着任意 exec 应用可向"XWayland 启动完成"信号投递假 ready。
- **修复**：child 分支 exec 前 `close_range(3, ~0u, 0)`（Linux ≥5.9），所有 socket/pipe 一律 `SOCK_CLOEXEC`/`pipe2(O_CLOEXEC)`；hyprutils `CProcess` 同样补 fd 屏障。

## F-PERM-03 ｜对话框特权按 **pid** 授予 → pid 消耗竞态可让恶意窗口"十年不可关闭"+优先焦点；X11 未排除 — 严重程度：中

- **位置**：`src/desktop/view/window/Window.cpp:1351-1353`（`isAsyncDialogBox(m_backend->pid())` → `m_closeableSince = +10y`，**没有** `!isX11()` 守卫）、`Window.cpp:1083-1085`（`priorityFocus()`：Wayland 客户端若 pid 命中 → 优先焦点）；登记表 `src/helpers/AsyncDialogBox.cpp:10`（`static vector<pair<pid_t, WP<CAsyncDialogBox>>>`）。
- **描述**：可信性完全绑定在**数值 pid** 上，而 pid 可由任何本地进程消耗性分配：恶意应用循环 `fork()` 把 `kernel.pid_max` 空间刷一遍（默认 32768/4194304，成本极低），必然命中某个打开中的对话框 pid 所在窗口 → 该窗口获得"10 年不可关闭"；Wayland 侧还可命中 `priorityFocus`（锁屏/防抢焦点场景下**强制把焦点抢给攻击者窗口**）。X11 客户端 pid 由 X 服务器报告（`_NET_WM_PID` 或 XCB-RES，见 08 报告 F-XW2-06），同样非强可信。
- **影响**：不可关闭的勒索/广告窗口；配合锁屏时对话框置顶语义做覆盖钓鱼。
- **修复**：登记对话框用其 **wl_client/WP 句柄**比对（`Window` 与 dialog 的 client 同一性校验），pid 仅作日志；`isAsyncDialogBox` 调用处补 `!isX11()`；pid 比对同时校验进程启动时间（`/proc/<pid>/stat` starttime）防复用。

## F-PERM-04 ｜权限规则缓存的 pid 绑定不随进程死亡失效 → pid 复用继承旧裁决 — 严重程度：低

- **位置**：`DynamicPermissionManager.cpp:173`（`e->m_keyString == str && pid == e->m_pid`）、`:351`；规则只在 wl_client 销毁时清（:17-21 的 destroy listener），**m_pid 关联的普通进程死了没有任何回收**。
- **描述**：`clientPermissionModeWithString` 类裁决（key 常为客户端可任意命名的字符串，如 virtual-keyboard 名称）按 (pid,key) 缓存。目标进程退出、pid 被回收且新进程用同名 key → 直接继承用户当年的 Allow/Deny。
- **修复**：记录 starttime 组成 (pid,starttime) 复合键；或 (key,pid) 规则加 TTL。

## F-PERM-05 ｜默认 `enforce_permissions=0`：整条权限/确认流水线旁路（上下文性发现） — 严重程度：信息（组合放大器）

- **位置**：`DynamicPermissionManager.cpp:81-84, 153-156`（`*PPERM == 0 → ALLOW`）。
- **描述**：默认配置下 screencopy、input-capture、keyboard、plugin 加载全部**无任何提示直接放行**。这与 07 报告的 F-PR2-06（锁屏截屏竞态）、F-PR2-05（截屏 OOM）、以及 hyprctl `plugin load`（F-IPC-03）串联后，"应用可以静默截屏/抓输入"成为出厂默认行为。安全语义上建议默认 ask（至少对 SCREENCOPY/INPUT_CAPTURE）。

## TokenManager 负面结果 + 低危注记（F-TOK-01）

- `src/managers/TokenManager.cpp:13-50`：`uuid_generate_random`（libuuid→/dev/urandom，正常）；`getToken` 读取时顺带清理过期 token ✓。两点注记：① `getToken` **不**删除 token（单次消费语义下放给调用方——调用方若忘记 `removeToken` 即重放窗口，建议提供 `consumeToken`）；② `registerNewToken` 无总量上限，token 发放入口若客户端可达则为累积 DoS。当前调用点少（activation），风险低。

## F-RULE-02 ｜exec_token 特权规则 = 环境共享秘密，任何客户端可自我声明冒领 — 严重程度：中

- **位置**：匹配端 `src/desktop/rule/windowRule/WindowRule.cpp:465-476`（`RULE_PROP_EXEC_TOKEN` 直接读 `w->getEnv()[HL_EXEC_RULE_TOKEN]` 与规则字符串比对）；注入端 `Executor.cpp:195-196`（`setenv(EXEC_RULE_ENV_NAME, token)`）；读取端 `Window.cpp:753-780`（`/proc/<pid>/environ`）。
- **描述**：token 是**配置文件里的明文共享秘密**（如社区惯例 `exec-rule[once] exec_token,polkitSecret foo` + `windowrulev2 ...,exec_token,polkitSecret`），无每启动随机化、无与 spawn 事件的绑定校验。恶意客户端只需在自己的 `main()` 前 `setenv("HL_EXEC_RULE_TOKEN","polkitSecret")` 再开窗，即完整继承为该特权启动器配置的规则集（典型：nofocus/pin/特殊工作区/lockout tag 联动——恰好是权限与锁屏语义最常用的地方）。子进程树自动带 token，链式传播无失效点。
- **修复**：token 改为**每次 spawn 由合成器生成的一次性随机值**（`exec-rule[once]` 已有 `expiresAt` 1 分钟，但值本身仍是配置明文），并在匹配时核对 (pid 启动时间 ≥ 规则激活时间)；`execExpired()` 命中的规则应同时使旧环境继承失效（按 pid 存活集白名单）。

## F-RULE-03 ｜exec_token 匹配同步读 /proc/<pid>/environ，主线程 I/O + 无缓存 — 严重程度：低（性能/放大面）

- **位置**：`Window.cpp:753-780`：每窗口映射、每条含 exec_token 的规则都会完整 `read()` 目标 environ（增长式缓冲，目标环境可达 2 MiB 上限）；`Window.cpp:1183` 也在映射路径调用。
- **描述**：窗口风暴（F-XW2-09 同源思路，但这里是 Wayland 侧批量建窗）× N 条 exec_token 规则 → 主线程同步 I/O 放大卡顿；非恶意场景同样存在性能问题。
- **修复**：按 pid 缓存 environ 解析结果（带 starttime 键），或仅在存在 exec_token 规则时惰性读取一次。

## F-PERM-06 ｜权限对话框描述注入客户端可控文本，无转义 → 富文本钓鱼 — 严重程度：低-中

- **位置**：`DynamicPermissionManager.cpp:240-262`（`{{app}}`/`{{plugin}}`/`{{keyboard}}` 占位符注入 i18n 模板）；模板含 `<b>...</b>`、调用处追加 `<br/><br/><i>`（:271）→ 描述是 **标记子集**（由 hyprland-dialog 渲染），但客户端可控字符串从未转义。
- **描述**：`binaryPath`（攻击者可控：自己把恶意二进制放到任意路径，canonical 后即显示名；见 F-PERM-01 关联）、virtual-keyboard **名称**（客户端任意字符串，:259）、plugin 路径（F-IPC-03 可达）都可携带 `<b>`/`<i>` 标记或换行伪造排版——把对话框文案改成"System component – required for GPU driver"类钓鱼话术，诱导用户点 Allow。i18n 侧无外部 .mo 加载（翻译硬编码，无解析攻击面），但**参数未做标记转义**是确定的 UI 欺骗原语。
- **修复**：占位符注入前做 HTML 转义（至少 `<>&` 与换行折叠），显示名截断长度。
