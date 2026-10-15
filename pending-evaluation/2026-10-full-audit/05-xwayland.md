# 审计域 05：XWayland（自主审计）

范围：`src/xwayland/`，威胁模型：**任何 X11 客户端（XWayland 内运行的恶意 GUI 应用）→ 合成器进程**。

---

## F-XW-01 ｜_NET_WM_STATE / _NET_WM_WINDOW_TYPE / WM_PROTOCOLS 解析忽略 format 字段 → 堆越界读 — 严重程度：高（X11 客户端可崩溃合成器）

- **位置**：`src/xwayland/XWM.cpp`
  - `handleWindowType`：`XWM.cpp:244-247`（`assign(atomsArr, atomsArr + reply->value_len)`，无 type/format 校验）
  - `handleWMState`：`XWM.cpp:249-256`（按 `xcb_atom_t`（4 字节）逐项读 `reply->value_len` 次，无 format 校验）
  - `handleWMProtocols`：`XWM.cpp:337-342`（校验了 `type == XCB_ATOM_ATOM` 但**未校验 `format == 32`**）
- **描述**：属性读取请求用 `XCB_ATOM_ANY`（`XWM.cpp:378`），X 服务器按客户端 change_property 时提交的 (type, format) 原样返回。X11 客户端可以为 `_NET_WM_STATE` 等 atom 数组属性提交 `format=8` 且 `value_len=2048` 的属性：此时 `xcb_get_property_value_length(reply)` 只有 2048 字节，但上述处理代码把 `value_len` 个元素按 **4 字节** 读取，遍历至偏移 `2048*4 = 8192` 字节 → 在 XCB 回复缓冲区上越界堆读最多 ~6KB。`std::vector::assign` 会整段复制 → OOB 数据进入 `XSURF->m_atoms` / `m_protocols`，且读越界极常见地触发 SIGSEGV，杀死整个合成器；即使不崩溃，越界字节会以 atom 值形式流入角色/模态判定（`m_modal`）。
- **PoC 思路**（X11 客户端，libxcb）：
  ```c
  uint8_t junk[2048] = {0};
  xcb_change_property(dpy, XCB_PROP_MODE_REPLACE, win,
      atom("_NET_WM_STATE"), XCB_ATOM_ATOM /*type*/, 8 /*format!!*/, 2048, junk);
  ```
  映射窗口触发 `readProp` 即崩。
- **修复**：三处统一加 `reply->format == 32 && reply->type == XCB_ATOM_ATOM` 守卫，并以 `value_length/4` 为元素个数上限：
  ```cpp
  if (reply->format != 32 || sc<size_t>(valueLen) < sc<size_t>(reply->value_len) * sizeof(xcb_atom_t)) return;
  ```

## 相关但风险较低（附带记录）

- `handleWMClass`（`XWM.cpp:220-227`）：`std::count('\0')==2` 后 `substr(find_first_of+1)` 再 `pop_back()`——若第二个 NUL 恰为最后一字节，`substr` 得空串，`pop_back` 因 `!empty()` 保护不触发，行为正确但脆弱（依赖隐含不变式）。
- `handleWMHints`/size hints（`XWM.cpp:258-266, 303-311`）：对 `value_len` 超限有校验，未见问题。
- 选区/`XCB_ICCCM_NUM_WM_HINTS_ELEMENTS` 路径未见未校验解引用。
