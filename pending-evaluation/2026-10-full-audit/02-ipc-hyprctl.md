# 安全审计报告 02 — IPC 攻击面（src/ipc、hyprctl、systemd、exec 链路）

- 仓库：/home/wilf/data/Hyprland（Hyprland v0.56.0 fork，配置系统已改为 Lua）
- 审计范围：src/ipc/s1（Commands.cpp/S1.cpp/Unix.cpp）、src/ipc/s2、hyprctl/、systemd/、exec 相关调用链（src/config/supplementary/executor/、src/config/lua/）
- 威胁模型：同机其他本地用户；同会话内运行的恶意 GUI 应用（经 Wayland 可读到 HYPRLAND_INSTANCE_SIGNATURE，天然可达 socket 目录）
- 审计日期：fork HEAD 397cb0efb

> 背景：本 fork 与上游差异巨大 —— 配置改为 Lua（`src/config/lua/`），IPC socket1 重写为命令注册表 + Lua eval 通路，socket 移入 `$XDG_RUNTIME_DIR/hypr/<随机签名>/`（这点比上游 /tmp 方案好）。以下发现多由 fork 引入。

---

## F-IPC-01 IPC `eval`/`repl` 命令 = 任意 Lua 代码执行（compositor 权限 RCE）

- **严重程度**：严重（Critical）
- **位置**：
  - `src/ipc/s1/Commands.cpp:1201-1216`（`evalRequest`，直接把请求剩余文本交给 `luaMgr->eval(code)`）
  - `src/ipc/s1/Commands.cpp:2010-2011`（注册 `eval`、`repl` 前缀命令）
  - `src/config/lua/ConfigManager.cpp:533`（`luaL_openlibs(m_lua)` 打开全部标准库，仅移除 `debug.sethook/gethook`（535-542 行）；`os`、`io`、`require` 全部可用）
  - `src/config/supplementary/executor/Executor.cpp:205`（`execl("/bin/sh", "/bin/sh", "-c", ...)`，Lua 侧 exec API 落到 shell）
- **攻击路径**：任何能 connect `.socket.sock` 的进程（恶意 GUI 应用天然可以）发送一条 IPC 请求即可获得 compositor 进程权限的 Lua 解释器：`os.execute` 任意命令执行；`io.open` 可改写 `~/.config/hypr/hyprland.lua` 实现持久化（下次登录以受害者全部桌面权限执行）；还可通过 `hl` API 重绑键、注入 event handler 窃取后续输入。
- **PoC 思路**：
  ```bash
  HYPRLAND_IS=$HYPRLAND_INSTANCE_SIGNATURE \
  python3 -c 'import socket;s=socket.socket();s.connect(f"/run/user/{os.getuid()}/hypr/{os.environ[\"HYPRLAND_IS\"]}/.socket.sock");s.sendall(b"eval os.execute(\"touch /tmp/IPC_PWNED\")");print(s.recv(4096))'
  ```
  （`hyprctl eval '...'` 亦可，但 IPC 直连不依赖 hyprctl。）
- **影响**：同用户 GUI 应用提权到"控制整个合成器"（键绑定、剪贴板、屏幕内容、配置持久化）；与 F-IPC-04 组合时升级为跨用户 RCE。
- **修复建议**：默认移除/禁用 `eval`、`repl` IPC 命令，或加运行期开关（默认 off）；Lua 状态用白名单标准库（去掉 `os.execute`、`io`、`loadfile`、`require` 路径注入）；调试功能改为需 `HYPRLAND_ENABLE_DEVEL` + 控制台二次确认。

---

## F-IPC-02 `dispatch` IPC 处理把用户输入拼接进 Lua 源码 → 代码注入

- **严重程度**：严重（Critical）
- **位置**：`src/ipc/s1/Commands.cpp:1217-1234`
  ```cpp
  std::string evalStr = std::format("return hl.dispatch({})", in);   // 1223
  auto ret = luaMgr->eval(evalStr)...;                               // 1224-1225
  ```
  `in`（攻击者可控的请求体）未经任何引用/转义直接内插进将要在 Lua 中求值的源码。1227 行注释甚至说明实现方预期用户传入含 `(` 的"lua dispatch string"。
- **攻击路径**：与 F-IPC-01 相同的 socket 面，且入口是 Hyprland 最常用、被各种脚本/状态栏/bar 按钮调用的 `dispatch` 命令 —— 任何以未信任数据调用 `hyprctl dispatch "$untrusted"` 的脚本都是注入点。
- **PoC 思路**（闭合括号 + `or` 绕过 `return` 位置限制）：
  ```text
  dispatch 1) or (function() os.execute("id > /tmp/pwned") end)() or (1
  ```
  拼接后：`return hl.dispatch(1) or (function() os.execute("id > /tmp/pwned") end)() or (1)` —— 合法 Lua，先执行副作用再返回。
- **影响**：同 F-IPC-01；且因为 `dispatch` 被大量第三方工具（waybar、rofi 脚本等）用外部可控参数调用，实际暴露面比 eval 更大。
- **修复建议**：不要用字符串拼接生成 Lua。将参数作为 Lua **字符串**安全下推（`lua_pushlstring` + `lua_setglobal("arg1")`，再执行固定片段 `return hl.dispatch(arg1)`），或在传统 C++ 分派表中解析 `dispatcher:arg` 而不是走 eval。

---

## F-IPC-03 IPC `plugin load` 允许 dlopen 任意路径 → 代码执行

- **严重程度**：高（High）
- **位置**：`src/ipc/s1/Commands.cpp:1770-1793`（`dispatchPlugin`：`plugin load <PATH>` 直接把请求里的 PATH 传给 `g_pPluginSystem->loadPlugin(...)`，1784 行）
- **攻击路径**：恶意 GUI 应用（同用户可写 /tmp 或自己家目录）先落一个恶意 .so，再发 `plugin load /tmp/evil.so`。插件以 compositor 进程权限运行任意构造函数，且可注册 event handler 长期驻留。未见任何路径白名单、签名/来源校验或目录限制（对比：至少应限制在用户显式配置的插件目录）。
- **PoC 思路**：编译含 `__attribute__((constructor)) system("...")` 的 .so，经 socket 发送 `plugin load /tmp/evil.so`。
- **影响**：compositor 权限代码执行；插件 ABI 回调还能直接操纵窗口/输入（比 eval 更隐蔽，卸载插件后已注册 dispatcher 可能残留）。
- **修复建议**：IPC 通道禁用 `plugin load/unload`（只允许多数情况下从配置文件加载）；若必须保留，校验路径位于白名单目录、属主/权限与请求者一致，并考虑要求 devel 模式。

---

## F-IPC-04 socket 访问控制只靠目录权限：无 uid 校验、bind 后不设权限，XDG_RUNTIME_DIR 异常时跨用户可达

- **严重程度**：高（High）
- **位置**：
  - `src/ipc/s1/Unix.cpp:54-64`（`peerPid()` 用 `SO_PEERCRED` **只取 pid，从不校验 uid/gid**）
  - `src/ipc/s1/Unix.cpp:241-256`（`bind()` 之后没有任何 `chmod/fchmod`；整个 src/ipc/ 中无 chmod —— grep 证实）。AF_UNIX bind 产生的 socket 文件模式为 `0777 & ~umask`（典型 0777/0755），connect 权限实际取决于**目录**权限位。
  - `src/Compositor.cpp:196-205`：XDG_RUNTIME_DIR 未设置以 `/hypr` 开头才 bail；形如非 `/run/user/...` 时**仅打印警告继续运行**（"Proceeding anyways"）。
  - 同样问题在 `src/ipc/s2/Unix.cpp:86-113`（socket2 全程无凭据检查）。
- **攻击路径**：若用户的 XDG_RUNTIME_DIR 被配置为共享路径（uwsm/systemd 环境、`XDG_RUNTIME_DIR=/tmp/...` 类魔改、容器化会话很常见），`.socket.sock` 文件本身是 `0666&~umask`，其他本地用户只要对该目录有 x 权限 + 猜/泄露签名（签名会出现在 `~/.local/state` 日志、`/proc/<pid>/environ` 对 root 可见、DEBUG 日志、以及任何继承环境变量的进程）即可连接，随后触发 F-IPC-01/02/03 → **跨用户 RCE**。即使签名未知，目录权限宽（如 0755 的自定义 runtime dir）时也无法靠权限位挡人。socket2 更简单：连上即持续收到窗口标题/焦点事件 → 跨用户窃听。
- **PoC 思路**：攻击者在能看到的目录枚举/爆破 `hypr/<commit>_<time>_<rand>`；或在 `XDG_RUNTIME_DIR` 指向 group 可读目录的环境里直接 `ls`。验证方式：`ls -l .socket.sock` 应为 0777&~umask 而非 0600。
- **影响**：把"同用户可达"的 IPC 抬升为"目录可达即可达"；丢失纵深防御。
- **修复建议**：`bind` 后立即 `fchmod`/`chmod` socket 为 0600；accept 时对 socket1/socket2 都做 `getsockopt(SO_PEERCRED)` 并强制 `cred.uid == getuid()`（root 连接也拒绝），不匹配直接 close；XDG_RUNTIME_DIR 非 `/run/user/<uid>` 时改为 bail 而非 warning。

---

## F-IPC-05 `c/systeminfo` 将完整配置文本回传任意 socket 对端（敏感信息泄露）

- **严重程度**：中（Medium）
- **位置**：
  - `src/ipc/s1/Commands.cpp:1185-1200`（`systemInfo(format, includeConfig)`；`includeConfig` 由 F-IPC-08 描述的 `c/` 请求前缀置位，`S1.cpp:68-76`）
  - 附带：`src/ipc/s1/Commands.cpp:1012-1023` + `src/ipc/s1/Unix.cpp:374-389`（`f/rollinglog` 进入 FOLLOW 模式持续推日志，日志常含窗口标题等 PII）
- **攻击路径**：恶意 GUI 应用发送 `c/systeminfo` 一次性拿走用户全部 Lua 配置——Hyprland 用户常把 `exec-once` 里的 token、密码、API key 或 `env=...` 写进配置；`f/rollinglog` 持续收割日志/标题。
- **PoC 思路**：向 socket 发送 `b"c/systeminfo"` 与 `b"f/rollinglog"` 观察响应。
- **影响**：凭据/隐私泄露给同机任意可达进程（配合 F-IPC-04 可跨用户）。
- **修复建议**：`c/` 仅允许本地 owner 且默认拒绝（devel 模式）；对输出中的 `exec*`/`env` 行做脱敏；rollinglog 跟随同样加 uid 校验。

---

## F-IPC-06 hyprctl 从不转义 `;` 与 `\`：批处理分隔符注入

- **严重程度**：中（Medium，脚本依赖型）
- **位置**：
  - `hyprctl/src/main.cpp:478`（`fullRequest += std::format("{} ", ARGS[i]);` 原样拼接 argv）
  - `hyprctl/src/main.cpp:347-357`（`batchRequest` 仅加 `[[BATCH]]` 前缀，不做转义）
  - 服务端 `src/ipc/s1/S1.cpp:129-151`（`dispatchBatch` 以裸 `;` 切分命令，`\;`/`\\` 为转义）
- **攻击路径**：任何用外部数据调用 hyprctl 的脚本（bar 点击绑定 `hyprctl dispatch "$(window_title)"`、`hyprctl batch "$user_input"`）中，数据含 `;` 即可把一条命令拆成多条 IPC 子命令，追加执行 `eval ...`/`plugin load ...`。分隔符注入 + F-IPC-01/02 构成完整链。另外 `S1.cpp:99` 前缀匹配无边界（`dispatchX` 也命中 `dispatch` 处理器）。
- **PoC 思路**：脚本 `hyprctl dispatch "focuswindow; eval os.execute(\"touch /tmp/pwn\")"`（经 `--batch` 或任何直接透传未信任文本的入口）；验证服务端把它拆成两条命令。
- **影响**：为脚本生态引入命令注入放大器。
- **修复建议**：hyprctl 在拼接请求时对 `\` 与 `;` 转义（服务端已支持 `\;`/`\\`，客户端却没生成过）；文档明确 batch 输入必须来自可信源；前缀匹配加空格/边界检查。

---

## F-IPC-07 同步执行 Lua/重查询阻塞合成器事件循环 + 无连接数上限 → DoS

- **严重程度**：低-中（Low/Medium）
- **位置**：
  - `src/ipc/s1/S1.cpp:205-215`（`dispatch()` 在 wl_event_loop 回调内同步跑完 handler；eval 死循环/重计算即冻结整个桌面）
  - `src/ipc/s1/Unix.cpp:283-301`（accept 循环无 `m_peers` 数量上限；`REQUEST_LIMIT` 1 MiB/连接，`Unix.cpp:43`；FOLLOW 队列 64 KiB/连接，`Unix.cpp:44`，仅约束单连接而非总量）
- **攻击路径**：恶意 GUI 应用（同用户）开数百连接 × 各 1 MiB 输入 + 64 KiB 输出即数百 MB 常驻；发 `eval "while true do end"` 直接挂死合成器。
- **PoC 思路**：脚本并发 connect 后发送大块数据/死循环 eval，观察合成器无响应与 RSS 增长。
- **影响**：会话可用性（DoS）；无法逃逸权限但可使桌面假死。
- **修复建议**：对 `m_peers` 设上限（如 64）并拒绝超额连接；给 eval 加 `lua_sethook` 指令计数看门狗（若 eval 保留的话）；单次请求 CPU 预算限制。

---

## F-IPC-08 请求格式解析宽松：未知标志字符静默忽略、无空格时的 `/` 截断（混淆类缺陷）

- **严重程度**：低（Low）
- **位置**：`src/ipc/s1/S1.cpp:46-83`（`parseRequest`）
  - 56-77 行：第一个 `/` 前的字符逐个比对 `j/r/a/f/c`，**未知字符只推进计数、不报错**——`xx/clients`、`jrx/clients` 都按默认格式通过，标志通道可被脚本误用/探测而无任何拒绝信号。
  - 52-58 行：只要整串含 `/` 且第一个 `/` 早于第一个空格，命令名即被截为 `substr(separator+1)`——参数中不含空格的 URL 型字符串（如 `dispatch open-folder/http://x`）会被改写命令名，产生分派混淆（配合 F-IPC-06 可被用来构造出人意料的等效请求）。
  - 99 行 `starts_with(command->name)` 前缀匹配无边界，见 F-IPC-06。
- **攻击路径**：单独不构成利用，但为注入/绕过提供了"等价请求"混淆面，且与 hyprctl 端不转义行为（F-IPC-06）叠加时放大不确定性。
- **PoC 思路**：对 socket 发送 `zz/clients` 验证被接受；`dispatch foo/bar` 与 `dispatch/bar` 的行为差异。
- **修复建议**：标志字符集白名单校验，未知字符直接 `unknown request`；前缀匹配要求命令名后是空格或串尾。

---

## 附注 / 已确认无问题的点

- socket 目录 `mkdir(S_IRWXU)`（`src/Compositor.cpp:229`，umask 只会收紧）且实例签名含 `random_device`（`Compositor.cpp:207-210`），**没有**沿用上游 /tmp 下可猜路径的问题；`XDG_RUNTIME_DIR` 未设置时路径以 `/hypr` 开头会 bail（Compositor.cpp:199-202），无 /tmp 竞争。
- socket1 读取有 1 MiB 上限 + 5 秒请求超时（`Unix.cpp:43, 81-91, 111-143`），无未初始化整数读取；socket2 每对端事件队列上限 64（`s2/Unix.cpp:191-206`）。未发现无界读取/整数溢出类缺陷。
- `systemd/hyprland-uwsm.desktop`、`systemd/hyprland-session.target`、`example/hyprland.service` 仅启动合成器，无特权位、无模板变量注入面；未见 IPC 相关的 systemd 加固问题（可在 unit 中加 `ProtectSystem=` 等属于建议而非漏洞）。
- socket2 中 `accept4` 误用 `sockaddr_in` 接收 unix 地址（`s2/Unix.cpp:131-133`）：类型错误但无害，建议顺手修正。

## 优先级修复顺序建议

1. F-IPC-02（改掉 dispatch 的字符串拼接 eval）与 F-IPC-01（默认关闭 IPC eval/repl + 裁剪 Lua 标准库）——两者同根源：**socket 可达 == compositor RCE**。
2. F-IPC-04（SO_PEERCRED uid 校验 + chmod 0600）——把可达性收回到内核强制的属主校验。
3. F-IPC-03、F-IPC-05。
4. F-IPC-06（hyprctl 转义）、F-IPC-07/08（加固项）。
