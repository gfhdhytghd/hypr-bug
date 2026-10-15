# 审计域 15：事件循环/定时器清理（自主审计）

## F-TMR-01 ｜CScreenshareSession 的 stop-timer 从不注销 → 会话销毁后定时器滞留 — 严重程度：低-中（累积性，含潜伏 UAF 模式）

- **位置**：注册 `src/managers/screenshare/ScreenshareSession.cpp:88-97`（`[this]` 捕获 + `addTimer`）；`stop()`（:61-67）与 `~CScreenshareSession()`（:55-59）**都没有** `removeTimer/cancel`（全文件仅 :88/:97 两处引用该 timer；对照正确姿势：`src/protocols/InputCapture.cpp:77-84` 析构里 removeTimer）。
- **描述**：每次截屏会话（客户端断开、窗口关闭、monitor 拔出触发 stop）销毁后，`SP<CEventLoopTimer>` 仍留在 `CEventLoopManager` 的定时器集合里：
  1. 最后一次帧后 ≤500ms 会对已销毁会话补发一次 `screenshareEvents(false)` 广播（当前回调体不解引用 `this`，侥幸不崩——属"依赖回调恰好不碰成员"的悬垂捕获地雷，任何后续改动即成真 UAF）；
  2. timer 对象及其闭包**永久滞留**在事件循环定时器容器内。恶意客户端反复 `create_session`+断开（默认 `enforce_permissions=0`，无需任何确认，F-PERM-05 联动）可无界累积定时器；`CEventLoopManager` 每轮需要遍历定时器算最近超时 → O(n) 每唤醒周期，n 只增不减 → 稳定劣化的事件循环延迟放大。
- **PoC 思路（文字）**：脚本循环 bind screencopy → create_session → destroy 数万次，观察 `hyprctl systeminfo` 内存/事件循环延迟。
- **修复**：析构与 `stop()` 中 `removeTimer(m_shareStopTimer)`；回调改用 `WP<CScreenshareSession>` 判活。

## 同模式巡检（其余 19 处 makeShared<CEventLoopTimer> 的 this 捕获）

- 单例管理器（ANRManager、KeybindManager、IPC/Unix、Renderer、WorkBufferPool、FallbackState）生命周期 = 进程，安全。
- InputCapture：析构 removeTimer ✓。
- Lua timer：`cleanTimers()` 先于 `lua_close` ✓（见 04 报告负面结果）。
- **唯一缺清理的即 F-TMR-01。**
