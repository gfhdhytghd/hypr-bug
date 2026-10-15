# 审计域 04：配置层 / Lua 绑定 / helpers（自主审计）

范围：`src/config/`（含 monitor/workspace 规则解析、ConfigWatcher）、`src/config/lua/`（抽查）、`src/helpers/`（ICC 解析、FsUtils）。
威胁模型：恶意/下载的 ICC 文件、损坏的配置输入、经 IPC 触发的配置重载。

---

## F-ICC-01 ｜恶意 ICC 文件触发堆越界读（vector operator[] 负/越界索引）— 严重程度：高（DoS，可能信息泄漏）

- **位置**：`src/helpers/cm/ICC.cpp:129-131`（`readVCGT16`）
- **描述**：
  ```cpp
  LOG(Log::DEBUG, "red channel: [{}, {}, ... {}, {}]",
      table.ch[0][0], table.ch[0][1], table.ch[0][table.entries - 2], table.ch[0][table.entries - 1]);
  ```
  `table.entries` 为 `uint16_t`（`src/helpers/cm/ColorManagement.hpp:194`），仅校验 `entries != 0`（`ICC.cpp:80-81`）。当 `entries == 1` 时：
  - `table.ch[c][1]` → 越界读（vector 大小为 1）；
  - `table.entries - 2` 经整型提升为 `int` = **-1**，传给 `operator[]` 后转成 `size_t` 巨值 → 堆上远端越界读，几乎必然 SIGSEGV。
  `LOG` 宏（`src/debug/log/Logger.hpp:10`）**无条件求值所有参数**（参数在调用点即绑定传入 `log(...)`），因此 release 构建同样触发。
  通过前面 `ch[0][entries-1] >= 30000` 检查（`ICC.cpp:124`）即可到达崩溃点，攻击者在构造 vcgt 时可完全控制该值。
- **前置条件**：`PVCGTENABLED` 打开（默认开）；加载一个含畸形 `vcgt` 标签的 ICC：
  `channels=3, entries=1, entrySize=2`，唯一条目值 ≥ 30000（如 0xFFFF），tag 长度满足 `8+4+2+2+2+2` 且 `raw.size() ≥ 20+6`。
- **攻击路径**：`monitor,<name>,...,icc,/path/to/evil.icc`（配置/IPC 重载均可触发 `loadICCFromDescription` → `readVCGT16`）。诱导用户加载"下载来的显示器校色文件"即合成器崩溃；越界读的字节还会被格式化进日志（信息泄漏面）。
- **修复**：`if (table.entries < 2) return unexpected` 或在打印前 clamp 索引；同时建议 LOG 宏对 debug 级延迟求值。

## F-ICC-02 ｜readBinary 对 tellg 失败值未检查 → 未捕获 bad_alloc 崩溃 — 严重程度：低

- **位置**：`src/helpers/cm/ICC.cpp:20-27`
- **描述**：`size_t len = ifs.tellg();` 当 `tellg()` 返回 -1（fifo/特殊文件 seek 失败）时，`len` 变为 `SIZE_MAX`；`if (len <= 0)` 对无符号数等价于 `== 0`，挡不住 -1 → `buf.resize(SIZE_MAX)` 抛 `std::bad_alloc`，调用链无 try/catch → `std::terminate`。
- **攻击路径**：把配置的 icc 路径指向命名管道/特殊文件即可让合成器 abort（需诱导用户配置）。
- **修复**：用 `std::streampos`/检查 `ifs.tellg() == -1`；或改用读取增长式读取。

## F-CFG-01 ｜monitor 规则解析 std::stoi 未捕获 out_of_range → 合成器 abort — 严重程度：低-中

- **位置**：`src/config/shared/monitor/Parser.cpp:221`（`parseTransform`）、`Parser.cpp:272`（`parseVRR`）
- **描述**：`isNumber(value)` 只保证"全是数字字符"，不限制长度。`std::stoi("99999999999999999999")` 抛 `std::out_of_range`，两处均无 try/catch（同文件 `parseSDRBrightness`(:249) 等则正确包了 catch，说明是疏漏）。异常逸出事件循环 → abort。
- **攻击路径**：Lua 配置的 monitor 规则表经 `src/config/lua/bindings/LuaBindingsConfigRules.cpp:84-115` 的 apply 函数直接调用这两个 parse 方法；结合本 fork IPC 的 `eval`/`keyword` 路径，可在运行期注入触发 → 桌面 DoS。
- **修复**：与兄弟函数一致地包 try/catch，或统一使用带范围检查的 `strToNumber`（workspace 规则里已在用，见 `WorkspaceRuleManager.cpp:91` 的安全实现）。

## F-RULE-01 ｜窗口规则正则未验证即使用（RE2 !ok() 状态参与匹配）— 严重程度：低

- **位置**：`src/desktop/rule/matchEngine/RegexMatchEngine.cpp:6-16`、构造点 `src/desktop/rule/Rule.cpp:98`
- **描述**：`RE2` 构造函数对非法模式不抛异常（`ok()==false`），代码从不检查 `m_regex->ok()`。之后每次窗口事件都调用 `re2::RE2::FullMatch(other, *m_regex)`。RE2 对 !ok 的模式使用属文档未定义行为：其内部 `Match` 带 `DFATAL`/DCHECK 级防护，**链接调试版 RE2 时恶意/笔误规则可让合成器 abort**；release 下则规则静默失效（比静默失效更糟的是与 `negative:` 组合后返回 `!= m_negative` → 反向放行）。
- **攻击路径**：windowrulev2 的 `matches,class,[(evil(regex` 类输入（配置作者失误或运行期规则注入）。
- **修复**：构造后检查 `ok()`，失败时回退到字面量匹配或标记规则无效并报错日志。

## 已检查、未见高危的区域（负面结果记录）

- `CConfigWatcher::onInotifyEvent`（`src/config/shared/inotify/ConfigWatcher.cpp:80-110`）：inotify 事件解析对 header/name 长度均有边界检查，`offset` 前进有 `bytesRead` 约束，无循环失控。
- Lua 对象层（`LuaWindow.cpp` 等）：统一使用 `WP.lock()` + expired 检查；`hl.timer` 的 `lua_State*` 捕获由 `cleanTimers()` 先于 `lua_close` 取消（`src/config/lua/ConfigManager.cpp:501-530`），并有 `SLuaStateLifetime` 守卫，重载入顺序正确。
- `setprop` 的 trivial 解析（`src/config/shared/actions/ConfigActions.cpp:794`、gradient 角度 `:847`）：均在 try/catch 内。
- 全 `src/` 无 `malloc/strcpy/sprintf` 使用（仅注释提及）。
